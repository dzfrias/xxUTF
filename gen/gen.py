#!/usr/bin/env python3

from itertools import batched
from dataclasses import dataclass


@dataclass
class DecompValue:
    decomps: list[int]
    ccc: int


DecompMap = dict[int, DecompValue]
CompMap = dict[tuple[int, int], int]


# Helper to recusively decompose a Unicode character `c`
def expand(c: int, map: DecompMap) -> list[int]:
    expansion = []
    stack = [c]
    while stack:
        x = stack.pop()
        if (
            x not in map
            or not map[x].decomps
            or (len(map[x].decomps) == 1 and x == map[x].decomps[0])
        ):
            expansion.append(x)
        elif map[x]:
            stack.extend(reversed(map[x].decomps))

    return expansion


# Guaranteed to be less than n.
# Taken from the unicode-rs/unicode-normalization project
def my_hash(x: int, salt: int, n: int) -> int:
    # This is hash based on the theory that multiplication is efficient
    mask_32 = 0xFFFFFFFF
    y = ((x + salt) * 2654435769) & mask_32
    y ^= (x * 0x31415926) & mask_32
    return (y * n) >> 32


# Compute minimal perfect hash function
# Taken from the unicode-rs/unicode-normalization project
def minimal_perfect_hash(d: DecompMap) -> tuple[list[int], list[int]]:
    n = len(d)
    buckets: dict[int, list[int]] = dict((h, []) for h in range(n))
    for key in d:
        h = my_hash(key, 0, n)
        buckets[h].append(key)
    bsorted = [(len(buckets[h]), h) for h in range(n)]
    bsorted.sort(reverse=True)
    claimed = [False] * n
    salts = [0] * n
    keys = [0] * n
    for bucket_size, h in bsorted:
        # Note: the traditional perfect hashing approach would also special-case
        # bucket_size == 1 here and assign any empty slot, rather than iterating
        # until rehash finds an empty slot. But we're not doing that so we can
        # avoid the branch.
        if bucket_size == 0:
            break
        else:
            for salt in range(1, 32768):
                rehashes = [my_hash(key, salt, n) for key in buckets[h]]
                # Make sure there are no rehash collisions within this bucket.
                if all(not claimed[hash] for hash in rehashes):
                    if len(set(rehashes)) < bucket_size:
                        continue
                    salts[h] = salt
                    for key in buckets[h]:
                        rehash = my_hash(key, salt, n)
                        claimed[rehash] = True
                        keys[rehash] = key
                    break
            if salts[h] == 0:
                print("minimal perfect hashing failed")
                # Note: if this happens (because of unfortunate data), then there are
                # a few things that could be done. First, the hash function could be
                # tweaked. Second, the bucket order could be scrambled (especially the
                # singletons). Right now, the buckets are sorted, which has the advantage
                # of being deterministic.
                #
                # As a more extreme approach, the singleton bucket optimization could be
                # applied (give the direct address for singleton buckets, rather than
                # relying on a rehash). That is definitely the more standard approach in
                # the minimal perfect hashing literature, but in testing the branch was a
                # significant slowdown.
                exit(1)
    return salts, keys


@dataclass
class TableInfo:
    nfd_chars_len: int
    nfd_len: int
    nfkd_chars_len: int
    nfkd_len: int
    comp_len: int


def generate_decomp_hash_table(
    writer, decomp_map: DecompMap, name: str
) -> tuple[int, int]:
    offsets = {}
    lengths = {}
    offset = 0
    all_decomps = []
    for k, decomp in decomp_map.items():
        offsets[k] = offset
        all_decomps.extend(decomp.decomps)
        lengths[k] = len(decomp.decomps)
        offset += len(decomp.decomps)
    assert offset <= 2**16 - 1

    writer.write(f"const uint32_t NORMDATA_{name}_CHARS[{len(all_decomps)}] = {{\n")
    for row in batched(all_decomps, 13):
        writer.write(" ")
        for b in row:
            writer.write(f" 0x{b:08X},")
        writer.write("\n")
    writer.write("};\n")

    decomp_salt, decomp_keys = minimal_perfect_hash(decomp_map)
    writer.write(f"\nconst uint16_t NORMDATA_{name}_SALT[{len(decomp_salt)}] = {{\n")
    for salts in batched(decomp_salt, 14):
        writer.write(" ")
        for s in salts:
            writer.write(f" 0x{s:04X},")
        writer.write("\n")
    writer.write("};\n")

    writer.write(
        f"\nconst NormdataTableEntry NORMDATA_{name}_KV[{len(decomp_keys)}] = {{\n"
    )
    for batch in batched(decomp_keys, 5):
        writer.write(" ")
        for k in batch:
            ccc = decomp_map[k].ccc
            writer.write(f" {{{lengths[k]}, {ccc}, 0x{offsets[k]:03X}, 0x{k:05X}}},")
        writer.write("\n")
    writer.write("};\n")

    return len(all_decomps), len(decomp_keys)


def generate_hash_tables(
    writer, nfd_map: DecompMap, nfkd_map: DecompMap, comp_map: CompMap
) -> TableInfo:
    nfd_bytes_len, nfd_len = generate_decomp_hash_table(writer, nfd_map, "NFD")
    nfkd_bytes_len, nfkd_len = generate_decomp_hash_table(writer, nfkd_map, "NFKD")

    # Write the composition map
    comp_table = {}
    for (c1, c2), x in comp_map.items():
        if c1 <= 0xFFFF and c2 <= 0xFFFF:
            comp_table[(c1 << 16) | c2] = x
    comp_salt, comp_keys = minimal_perfect_hash(comp_table)
    writer.write(f"\nconst uint16_t NORMDATA_NFC_SALT[{len(comp_salt)}] = {{\n")
    for salts in batched(comp_salt, 14):
        writer.write(" ")
        for s in salts:
            writer.write(f" 0x{s:04X},")
        writer.write("\n")
    writer.write("};\n")
    writer.write(f"\nconst uint32_t NORMDATA_NFC_KV[{len(comp_keys)}][2] = {{\n")
    for batch in batched(comp_keys, 8):
        writer.write(" ")
        for k in batch:
            comp = comp_table[k]
            writer.write(f" {{0x{comp:05X}, 0x{k:08X}}},")
        writer.write("\n")
    writer.write("};\n")

    writer.write(
        "\nuint32_t normdata_compose_supplementary(uint32_t c1, uint32_t c2) {\n"
    )
    for (c1, c2), x in comp_map.items():
        if c1 <= 0xFFFF and c2 <= 0xFFFF:
            continue
        writer.write(f"  if (c1 == 0x{c1:08X} && c2 == 0x{c2:08X}) return 0x{x:08X};\n")
    writer.write("  return 0;\n}\n")

    return TableInfo(
        nfd_chars_len=nfd_bytes_len,
        nfd_len=nfd_len,
        nfkd_chars_len=nfkd_bytes_len,
        nfkd_len=nfkd_len,
        comp_len=len(comp_keys),
    )


def is_bit_set(mask: int, i: int) -> bool:
    return (mask & (1 << i)) == (1 << i)


def compute_locations(mask: int) -> list[int]:
    answer: list[int] = []
    i = 0
    while (mask >> i) > 0:
        if is_bit_set(mask, i):
            answer.append(i)
        i += 1
    return answer


def compute_code_point_size(mask: int) -> list[int]:
    positions = compute_locations(mask)
    answer = []
    old_x = -1
    for i in range(len(positions)):
        x = positions[i]
        answer.append(x - old_x)
        old_x = x
    return answer


def has_code_points_up_to_size(sizes: list[int], size: int, n: int) -> bool:
    if len(sizes) < n:
        return False
    return max(sizes[:n]) <= size


def build_shuf(sizes: tuple[int, ...]) -> list[int]:
    answer = [0] * 16
    pos = 0
    if max(sizes) <= 2 and len(sizes) == 4:
        for i in range(4):
            if sizes[i] == 1:
                answer[i * 2] = pos
                answer[i * 2 + 1] = 0xFF
                pos += 1
            else:
                assert sizes[i] == 2
                answer[i * 2] = pos + 1
                answer[i * 2 + 1] = pos
                pos += 2
    else:
        assert len(sizes) == 4 or len(sizes) == 3
        for i in range(len(sizes)):
            for j in range(12 // len(sizes)):
                if sizes[i] != j + 1:
                    continue
                answer[i * 4] = pos + j
                answer[i * 4 + 1] = 0xFF if j - 1 < 0 else pos + (j - 1)
                answer[i * 4 + 2] = 0xFF if j - 2 < 0 else pos + (j - 2)
                answer[i * 4 + 3] = 0xFF if j - 3 < 0 else pos + (j - 3)
                pos += j + 1
                break

    return answer


@dataclass
class ShuffleInfo:
    shufutf8_len: int
    codepoint_index_len: int


def generate_shuffle_tables(writer) -> ShuffleInfo:
    case12_set: set[tuple[int, ...]] = set()
    case123_set: set[tuple[int, ...]] = set()
    case1234_set: set[tuple[int, ...]] = set()
    for x in range(1 << 12):
        sizes = compute_code_point_size(x)
        if has_code_points_up_to_size(sizes, size=2, n=4):
            case12_set.add(tuple(sizes[:4]))
        elif has_code_points_up_to_size(sizes, size=3, n=4):
            case123_set.add(tuple(sizes[:4]))
        elif has_code_points_up_to_size(sizes, size=4, n=3):
            case1234_set.add(tuple(sizes[:3]))
    case12 = sorted(case12_set)
    case123 = sorted(case123_set)
    case1234 = sorted(case1234_set)
    cases = case12 + case123 + case1234

    all_shuf = [build_shuf(z) for z in cases]
    writer.write(f"\nconst uint8_t NORMDATA_SHUFUTF8[{len(cases)}][16] = {{\n")
    for shuf in all_shuf:
        writer.write(f"  {{{", ".join(map(str, shuf))}}},\n")
    writer.write("};\n")

    index = {t: i for i, t in enumerate(cases)}
    arrg = []
    for x in range(1 << 12):
        sizes = compute_code_point_size(x)
        if has_code_points_up_to_size(sizes, size=2, n=4):
            idx = index[tuple(sizes[:4])]
            arrg.append((idx, sum(sizes[:4])))
        elif has_code_points_up_to_size(sizes, size=3, n=4):
            idx = index[tuple(sizes[:4])]
            arrg.append((idx, sum(sizes[:4])))
        elif has_code_points_up_to_size(sizes, size=4, n=3):
            idx = index[tuple(sizes[:3])]
            arrg.append((idx, sum(sizes[:3])))
        else:
            arrg.append((len(all_shuf), 12))

    writer.write(f"\nconst uint8_t NORMDATA_CODEPOINT_INDEX[{len(arrg)}][2] = {{\n")
    for row in batched(arrg, 8):
        writer.write(" ")
        for a in row:
            writer.write(f" {{{", ".join(map(str, a))}}},")
        writer.write("\n")
    writer.write("};\n")

    writer.write(f"\nconst uint8_t NORMDATA_SHUFUTF8_INDEX_12 = {len(case12)};\n")
    writer.write(
        f"const uint8_t NORMDATA_SHUFUTF8_INDEX_123 = {len(case12) + len(case123)};\n"
    )
    writer.write(f"const uint8_t NORMDATA_SHUFUTF8_INDEX_1234 = {len(cases)};\n")

    writer.write(f"\nconst NormdataHangulShuf NORMDATA_HANGUL_SHUF[16] = {{\n")
    for x in range(1 << 4):
        exclude = []
        total_size = 0
        for i, bit in enumerate(reversed(f"{x:04b}")):
            if bit == "1":
                exclude.append(4 + i * 6)
                exclude.append(5 + i * 6)
                total_size += 6
            else:
                total_size += 9
        tbl = [x for x in range(24) if x not in exclude]
        # Pad to be 24 in length
        tbl.extend([255] * (24 - len(tbl)))
        writer.write(f"  {{{total_size}, {{{", ".join(map(str, tbl))}}}}},\n")
    writer.write("};\n")

    return ShuffleInfo(shufutf8_len=len(cases), codepoint_index_len=len(arrg))


@dataclass
class BloomFilterInfo:
    blocks: list[int]


def xorshift_hash(x: int, seed: int = 0) -> int:
    mask_32 = 0xFFFFFFFF
    x ^= seed
    x ^= (x >> 13) & mask_32
    x ^= (x << 17) & mask_32
    x ^= (x >> 5) & mask_32
    return x


def multiply_shift_hash(x: int) -> int:
    return ((2654435761 * x) >> 16) & ((1 << 16) - 1)


def hash_32bit_fast(x, seed=0):
    mask_32 = 0xFFFFFFFF
    x ^= seed
    x = (((x >> 16) ^ x) * 0x45D9F3B) & mask_32
    x = (((x >> 16) ^ x) * 0x45D9F3B) & mask_32
    x = ((x >> 16) ^ x) & mask_32
    return x


class BloomFilter:
    def __init__(self, size: int, block_fn, hash_fns: list, data: list[int]) -> None:
        self.size = size
        self.block_fn = block_fn
        self.hash_fns = hash_fns
        self.data = data

        self.blocks = [0] * self.n_blocks()
        for c in data:
            block, mask = self.create_mask(c)
            self.blocks[block] |= mask

    def create_mask(self, c: int) -> tuple[int, int]:
        block = self.block_fn(c) % self.n_blocks()
        mask = 0
        for hash_fn in self.hash_fns:
            h = hash_fn(c)
            shift = h % 32
            mask |= 1 << shift
        return block, mask

    def false_positives(self, universe) -> tuple[list[int], float]:
        fps = []
        n = 0
        for c in universe:
            n += 1
            block_idx, mask = self.create_mask(c)
            block = self.blocks[block_idx]
            if (block & mask) == mask and c not in self.data:
                fps.append(c)
        return fps, len(fps) / n

    def n_blocks(self) -> int:
        return self.size // 32


def generate_bloom_filter(writer, name: str, bloom: BloomFilter):
    writer.write(f"\nconst uint32_t {name}[{bloom.n_blocks()}] = {{\n")
    for row in batched(bloom.blocks, 10):
        writer.write(" ")
        for block in row:
            writer.write(f" 0x{block:08X},")
        writer.write("\n")
    writer.write("};\n")


def generate_header(
    writer,
    table_info: TableInfo,
    shuf_info: ShuffleInfo,
    nfd_bloom: BloomFilter,
    nfkd_bloom: BloomFilter,
    nfc_bloom: BloomFilter,
    nfkc_bloom: BloomFilter,
    non_starters_bloom: BloomFilter,
):
    writer.write(
        f"extern const uint32_t NORMDATA_NFD_CHARS[{table_info.nfd_chars_len}];\n"
    )
    writer.write(f"extern const uint16_t NORMDATA_NFD_SALT[{table_info.nfd_len}];\n")
    writer.write(
        f"extern const NormdataTableEntry NORMDATA_NFD_KV[{table_info.nfd_len}];\n"
    )
    writer.write(
        f"extern const uint32_t NORMDATA_NFKD_CHARS[{table_info.nfkd_chars_len}];\n"
    )
    writer.write(f"extern const uint16_t NORMDATA_NFKD_SALT[{table_info.nfkd_len}];\n")
    writer.write(
        f"extern const NormdataTableEntry NORMDATA_NFKD_KV[{table_info.nfkd_len}];\n"
    )
    writer.write(f"extern const uint16_t NORMDATA_NFC_SALT[{table_info.comp_len}];\n")
    writer.write(f"extern const uint32_t NORMDATA_NFC_KV[{table_info.comp_len}][2];\n")
    writer.write(
        f"extern const uint8_t NORMDATA_SHUFUTF8[{shuf_info.shufutf8_len}][16];\n"
    )
    writer.write(
        f"extern const uint8_t NORMDATA_CODEPOINT_INDEX[{shuf_info.codepoint_index_len}][2];\n"
    )
    writer.write(f"extern const uint8_t NORMDATA_SHUFUTF8_INDEX_12;\n")
    writer.write(f"extern const uint8_t NORMDATA_SHUFUTF8_INDEX_123;\n")
    writer.write(f"extern const uint8_t NORMDATA_SHUFUTF8_INDEX_1234;\n")
    writer.write(f"extern const NormdataHangulShuf NORMDATA_HANGUL_SHUF[16];\n")
    writer.write(
        f"extern const uint32_t NORMDATA_NFD_BLOOM_FILTER[{nfd_bloom.n_blocks()}];\n"
    )
    writer.write(
        f"extern const uint32_t NORMDATA_NFKD_BLOOM_FILTER[{nfkd_bloom.n_blocks()}];\n"
    )
    writer.write(
        f"extern const uint32_t NORMDATA_NFC_BLOOM_FILTER[{nfc_bloom.n_blocks()}];\n"
    )
    writer.write(
        f"extern const uint32_t NORMDATA_NFKC_BLOOM_FILTER[{nfkc_bloom.n_blocks()}];\n"
    )
    writer.write(
        f"extern const uint32_t NORMDATA_NON_STARTERS_BLOOM_FILTER[{non_starters_bloom.n_blocks()}];\n"
    )


PREAMBLE_H = """// This file was generated by gen/gen.py

#ifndef UTF8NORM_NORMDATA_H
#define UTF8NORM_NORMDATA_H

#include <stdint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

typedef struct NormdataTableEntry {
  uint8_t len;
  uint8_t ccc;
  uint16_t offset;
  uint32_t k;
} NormdataTableEntry;

typedef struct NormdataHangulShuf {
  uint8_t len;
  uint8_t tbl[24];
} NormdataHangulShuf;

static const uint16_t NORMDATA_S_BASE = 0xAC00;
static const uint16_t NORMDATA_L_BASE = 0x1100;
static const uint16_t NORMDATA_V_BASE = 0x1161;
static const uint16_t NORMDATA_T_BASE = 0x11A7;
static const uint16_t NORMDATA_L_COUNT = 19;
static const uint16_t NORMDATA_V_COUNT = 21;
static const uint16_t NORMDATA_T_COUNT = 28;
static const uint16_t NORMDATA_N_COUNT = NORMDATA_V_COUNT * NORMDATA_T_COUNT;
static const uint16_t NORMDATA_S_COUNT = NORMDATA_L_COUNT * NORMDATA_N_COUNT;

uint32_t normdata_compose_supplementary(uint32_t c1, uint32_t c2);

"""

POSTAMBLE_H = """
static const uint32_t NORMDATA_NFD_TABLE_SIZE = sizeof(NORMDATA_NFD_KV) / sizeof(NormdataTableEntry);
static const uint32_t NORMDATA_NFKD_TABLE_SIZE = sizeof(NORMDATA_NFKD_KV) / sizeof(NormdataTableEntry);
static const uint32_t NORMDATA_NFC_TABLE_SIZE = sizeof(NORMDATA_NFC_KV) / sizeof(uint64_t);

static const uint8_t NORMDATA_UTF8_SIZE[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0};

#pragma GCC diagnostic pop

#endif // UTF8NORM_NORMDATA_H
"""

PREAMBLE = """// This file was generated by gen/gen.py

#include "normdata.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

"""

POSTAMBLE = """

#pragma GCC diagnostic pop
"""


def code_points():
    with open("UnicodeData.txt", "r") as f:
        range_start = 0

        for line in f:
            info = line.split(";")
            value = int(info[0], 16)
            name = info[1]

            if name.endswith("First>"):
                range_start = value
                continue
            if name.endswith("Last>"):
                for x in range(range_start, value + 1):
                    yield x
                continue

            yield value


def bloom_filter_fps(
    code_points, info: BloomFilterInfo, mask_fn, negative_fn
) -> tuple[list[int], float]:
    fps = []
    n = 0
    for c in code_points:
        n += 1
        block_idx, mask = mask_fn(c)
        block = info.blocks[block_idx]
        if (block & mask) == mask and negative_fn(c):
            fps.append(c)
    return fps, len(fps) / n


def align_key_value_lines(lines: list[str]) -> list[str]:
    """
    Aligns a list of strings of the form 'key: value' so that the colons line up.
    """
    # Split each line into key and value
    split_lines = [line.split(":", 1) for line in lines]

    # Strip whitespace and find the longest key
    max_key_len = max(len(key.strip()) for key, _ in split_lines)

    # Format each line with aligned colon
    aligned = [
        f"{key.strip():<{max_key_len}} {value.strip()}" for key, value in split_lines
    ]
    return aligned


# Load NFD and NFKD decomposition maps from `UnicodeData.txt`.
def load_decomp_maps() -> tuple[DecompMap, DecompMap]:
    nfd_map: DecompMap = {}
    nfkd_map: DecompMap = {}

    # Read in UnicodeData.txt and generate a decomposition mapping
    with open("UnicodeData.txt", "r") as f:
        for line in f:
            info = line.split(";")
            value = int(info[0], 16)
            mappings = info[5].split(" ")
            ccc = int(info[3])

            if ccc > 0:
                # Add all CCC > 0 characters to the decomp map
                nfd_map[value] = DecompValue([value], ccc)
                nfkd_map[value] = DecompValue([value], ccc)

            # Skip decomp if there is nothing or if it is a compatibility decomposition
            if mappings[0] == "":
                continue

            if mappings[0].startswith("<"):
                assert len(mappings) > 1
                nfkd_map[value] = DecompValue([int(x, 16) for x in mappings[1:]], ccc)
                continue

            nfd_map[value] = DecompValue([int(x, 16) for x in mappings], ccc)
            nfkd_map[value] = DecompValue([int(x, 16) for x in mappings], ccc)

    return nfd_map, nfkd_map


@dataclass
class DerivedProps:
    comp_exclusions: list[int]
    nfc_qc: list[int]
    nfkc_qc: list[int]


def load_derived_props() -> DerivedProps:
    exclusions = []
    nfc_qc = []
    nfkc_qc = []

    with open("DerivedNormalizationProps.txt", "r") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue

            parts = line.split(";")
            raw_code_points = parts[0].strip().split("..")
            assert len(raw_code_points) <= 2
            if len(raw_code_points) == 1:
                c = int(raw_code_points[0], 16)
                code_points = range(c, c + 1)
            else:
                start = int(raw_code_points[0], 16)
                end = int(raw_code_points[1], 16)
                code_points = range(start, end + 1)

            if "Full_Composition_Exclusion" in line:
                exclusions.extend(code_points)
            if "NFC_QC" in line:
                nfc_qc.extend(code_points)
            if "NFKC_QC" in line:
                nfkc_qc.extend(code_points)

    return DerivedProps(comp_exclusions=exclusions, nfc_qc=nfc_qc, nfkc_qc=nfkc_qc)


# Flatten a decomposition map so that any recursive decompositions are resolved.
def flatten_decomp_map(map: DecompMap):
    for x, decomp in map.items():
        final_decomp: list[int] = []
        for c in decomp.decomps:
            # Get the full expansion of each code point that makes up `x`
            expansion = expand(c, map)
            final_decomp.extend(expansion)
        map[x].decomps = final_decomp


def main() -> None:
    nfd_map, nfkd_map = load_decomp_maps()
    derived = load_derived_props()

    non_starters = [x for x, decomp in nfd_map.items() if decomp.ccc > 0]

    comp_map: CompMap = {}
    for x, decomp in nfd_map.items():
        if x in derived.comp_exclusions or decomp.decomps[0] == x:
            continue
        assert len(decomp.decomps) == 2
        comp_map[(decomp.decomps[0], decomp.decomps[1])] = x

    flatten_decomp_map(nfd_map)
    flatten_decomp_map(nfkd_map)

    for x, decomps in nfkd_map.items():
        if (
            decomps.ccc == 0
            and decomps.decomps
            and all(d in nfkd_map and nfkd_map[d].ccc > 0 for d in decomps.decomps)
        ):
            # HACK: this is a very implementation-specific operation, but here's my best
            #       explanation: we want to ensure that any character that decomposes
            #       into combining characters (all ccc values > 0) also has a ccc value > 0.
            #       This is important, because one way we detect for when we need to do a
            #       combining character sort is by looking at the original (precomposed)
            #       character's ccc value. There are a few code points that have a ccc value
            #       of zero, yet decompose solely into code points with ccc values > 0. This
            #       amends those characters so that they can be properly detected as combining
            #       marks. Obviously, patching over the Unicode character database is suboptimal,
            #       but this presently causes no issues with the decompositon process.
            #       See https://corp.unicode.org/pipermail/unicode/2025-July/011511.html for the
            #       relevant discussion on this. It might also be a more convincing argument
            #       for why this operation doesn't mess with the canonical decomposition process
            #       in a harmful way.
            #
            #       From https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G49537:
            #       > All characters with non-zero canonical combining class are combining characters,
            #       > but the reverse is not the case: there are combining characters with a zero
            #       > canonical combining class.
            #       This change makes "the reverse" true: x.ccc > 0 iff x is a combining character.
            nfkd_map[x].ccc = 1
            if x in nfd_map:
                nfd_map[x].ccc = 1

    with open("normdata.c", "w") as f:
        f.write(PREAMBLE)
        hash_info = generate_hash_tables(f, nfd_map, nfkd_map, comp_map)
        shuf_info = generate_shuffle_tables(f)
        nfd_bloom = BloomFilter(
            131072,
            multiply_shift_hash,
            [
                xorshift_hash,
                hash_32bit_fast,
                lambda c: xorshift_hash(c) + hash_32bit_fast(c),
            ],
            list(nfd_map.keys()),
        )
        generate_bloom_filter(f, "NORMDATA_NFD_BLOOM_FILTER", nfd_bloom)
        nfkd_bloom = BloomFilter(
            262144,
            multiply_shift_hash,
            [
                xorshift_hash,
                hash_32bit_fast,
                lambda c: xorshift_hash(c) + hash_32bit_fast(c),
            ],
            list(nfkd_map.keys()),
        )
        generate_bloom_filter(f, "NORMDATA_NFKD_BLOOM_FILTER", nfkd_bloom)
        nfc_bloom = BloomFilter(
            131072,
            multiply_shift_hash,
            [
                xorshift_hash,
                lambda c: hash_32bit_fast(c ^ 0xDEADBEEF),
                hash_32bit_fast,
            ],
            derived.nfc_qc,
        )
        generate_bloom_filter(f, "NORMDATA_NFC_BLOOM_FILTER", nfc_bloom)
        nfkc_bloom = BloomFilter(
            131072,
            multiply_shift_hash,
            [
                xorshift_hash,
                lambda c: hash_32bit_fast(c ^ 0xDEADBEEF),
                hash_32bit_fast,
            ],
            derived.nfkc_qc,
        )
        generate_bloom_filter(f, "NORMDATA_NFKC_BLOOM_FILTER", nfkc_bloom)
        non_starters_bloom = BloomFilter(
            131072,
            multiply_shift_hash,
            [
                xorshift_hash,
                lambda c: hash_32bit_fast(c ^ 0xDEADBEEF),
                hash_32bit_fast,
            ],
            non_starters,
        )
        generate_bloom_filter(
            f, "NORMDATA_NON_STARTERS_BLOOM_FILTER", non_starters_bloom
        )
        f.write(POSTAMBLE)
    with open("normdata.h", "w") as f:
        f.write(PREAMBLE_H)
        generate_header(
            f,
            hash_info,
            shuf_info,
            nfd_bloom,
            nfkd_bloom,
            nfc_bloom,
            nfkc_bloom,
            non_starters_bloom,
        )
        f.write(POSTAMBLE_H)

    KILOBYTE = 1024
    lines = []
    lines.append(f"Decomposed chars: {hash_info.nfd_chars_len / KILOBYTE:.1f}KiB")
    lines.append(f"Decomposed salt: {(hash_info.nfd_len * 2) / KILOBYTE:.1f}KiB")
    lines.append(f"Decomposed KV: {(hash_info.nfd_len * 8) / KILOBYTE:.1f}KiB")
    lines.append(f"Composition salt: {(hash_info.comp_len * 2) / KILOBYTE:.1f}KiB")
    lines.append(f"Composition KV: {(hash_info.comp_len * 8) / KILOBYTE:.1f}KiB")
    lines.append(f"UTF-8 shuffle: {(shuf_info.shufutf8_len * 16) / KILOBYTE:.1f}KiB")
    lines.append(
        f"Code point index: {(shuf_info.codepoint_index_len * 2) / KILOBYTE:.1f}KiB"
    )

    lines.append(f"NFD bloom filter: {(nfd_bloom.n_blocks() * 4) / KILOBYTE:.1f}KiB")
    fp_list, fpr = nfd_bloom.false_positives(code_points())
    lines.append(f"NFD bloom filter FPR: {fpr:.5f}")
    lines.append(f"NFD bloom filter BMP: {len([c for c in fp_list if c <= 0xFFFF])}")
    lines.append(f"NFD bloom filter ASCII: {len([c for c in fp_list if c < 128])}")

    lines.append(f"NFKD bloom filter: {(nfkd_bloom.n_blocks() * 4) / KILOBYTE:.1f}KiB")
    fp_list, fpr = nfkd_bloom.false_positives(code_points())
    lines.append(f"NFKD bloom filter FPR: {fpr:.5f}")
    lines.append(f"NFKD bloom filter BMP: {len([c for c in fp_list if c <= 0xFFFF])}")
    lines.append(f"NFKD bloom filter ASCII: {len([c for c in fp_list if c < 128])}")

    lines.append(f"NFC QC bloom filter: {(nfc_bloom.n_blocks() * 4) / KILOBYTE:.1f}KiB")
    fp_list, fpr = nfc_bloom.false_positives(code_points())
    # TODO: might be worth it to make a BMP-specific bloom filter
    lines.append(f"NFC QC bloom filter FPR: {fpr:.5f}")
    lines.append(f"NFC QC bloom filter BMP: {len([c for c in fp_list if c <= 0xFFFF])}")
    lines.append(f"NFC QC bloom filter ASCII: {len([c for c in fp_list if c < 128])}")

    lines.append(
        f"NFKC QC bloom filter: {(nfkc_bloom.n_blocks() * 4) / KILOBYTE:.1f}KiB"
    )
    fp_list, fpr = nfkc_bloom.false_positives(code_points())
    lines.append(f"NFKC QC bloom filter FPR: {fpr:.5f}")
    lines.append(
        f"NFKC QC bloom filter BMP: {len([c for c in fp_list if c <= 0xFFFF])}"
    )
    lines.append(f"NFKC QC bloom filter ASCII: {len([c for c in fp_list if c < 128])}")

    fp_list, fpr = non_starters_bloom.false_positives(code_points())
    lines.append(f"ccc > 0 bloom filter FPR: {fpr:.5f}")
    lines.append(
        f"ccc > 0 bloom filter BMP: {len([c for c in fp_list if c <= 0xFFFF])}"
    )
    lines.append(f"ccc > 0 bloom filter ASCII: {len([c for c in fp_list if c < 128])}")

    aligned = align_key_value_lines(lines)
    for line in aligned:
        print(line)


if __name__ == "__main__":
    main()
