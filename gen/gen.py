#!/usr/bin/env python3

from itertools import batched
from dataclasses import dataclass
from trie import Trie


@dataclass
class DecompValue:
    decomps: list[int]
    ccc: int


DecompMap = dict[int, DecompValue]
CasefoldMap = dict[int, list[int]]
CompMap = dict[tuple[int, int], int]


# Helper to recursively decompose a Unicode character `c`
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


class HeaderDef:
    def __init__(self, name: str, type_: str):
        self.name = name
        self.type_ = type_
        self.array_sizes: list[int] = []

    @classmethod
    def array(cls, name: str, type_: str, array_size: int):
        inst = cls(name, type_)
        inst.array_sizes.append(array_size)
        return inst

    @classmethod
    def multi_array(cls, name: str, type_: str, array_sizes: list[int]):
        inst = cls(name, type_)
        inst.array_sizes = array_sizes
        return inst

    def size(self, element_size: int) -> int:
        product = element_size
        for size in self.array_sizes:
            product *= size
        return product


def generate_array(writer, name: str, data: list[int], data_width: int) -> HeaderDef:
    writer.write(f"\nconst uint{data_width}_t {name}[{len(data)}] = {{\n")
    for row in batched(data, 10):
        writer.write(" ")
        for x in row:
            # TODO: handle negative numbers
            assert x < 2**data_width
            if data_width == 32:
                writer.write(f" 0x{x:08X},")
            elif data_width == 16:
                writer.write(f" 0x{x:04X},")
            elif data_width == 8:
                writer.write(f" 0x{x:02X},")
            else:
                raise ValueError(f"Unknown data width: {data_width}")
        writer.write("\n")
    writer.write("};\n")
    return HeaderDef.array(name, f"uint{data_width}_t", len(data))


def generate_decomp_hash_table(
    writer, decomp_map: DecompMap, name: str
) -> list[HeaderDef]:
    supplementary_map: DecompMap = {k: v for k, v in decomp_map.items() if k > 0xFFFF}

    offsets = {}
    lengths = {}
    offset = 0
    all_decomps = []
    for k, decomp in supplementary_map.items():
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

    decomp_salt, decomp_keys = minimal_perfect_hash(supplementary_map)
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
            decomp = supplementary_map[k]
            ccc_vals = [
                decomp_map.get(a, DecompValue([], 0)).ccc for a in decomp.decomps
            ]
            last_ccc = ccc_vals[-1]
            if (
                len(decomp.decomps) > 1
                and any(ccc < last_ccc and ccc != 0 for ccc in ccc_vals)
                and ccc_vals[0] != 0
            ):
                print("Detected complex ccc decomposition!")
                sys.exit(1)
            ccc = supplementary_map[k].ccc
            writer.write(
                f" {{{lengths[k]}, {ccc}, {last_ccc}, 0x{offsets[k]:03X}, 0x{k:05X}}},"
            )
        writer.write("\n")
    writer.write("};\n")

    return [
        HeaderDef.array(f"NORMDATA_{name}_CHARS", "uint32_t", len(all_decomps)),
        HeaderDef.array(f"NORMDATA_{name}_SALT", "uint16_t", len(decomp_salt)),
        HeaderDef.array(f"NORMDATA_{name}_KV", "NormdataTableEntry", len(decomp_keys)),
    ]


def generate_hash_tables(
    writer,
    nfd_map: DecompMap,
    nfkd_map: DecompMap,
    comp_map: CompMap,
    casefold_map: CasefoldMap,
) -> list[HeaderDef]:
    headers = []
    headers.extend(generate_decomp_hash_table(writer, nfd_map, "NFD"))
    headers.extend(generate_decomp_hash_table(writer, nfkd_map, "NFKD"))

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

    headers.extend(
        [
            HeaderDef.array("NORMDATA_NFC_SALT", "uint16_t", len(comp_salt)),
            HeaderDef.multi_array("NORMDATA_NFC_KV", "uint32_t", [len(comp_keys), 2]),
        ]
    )

    # TODO: casefold hash table. All supplementary plane case fold mappings are common,
    # so we don't need much information in the hash table
    casefold_table = {}
    for k, v in casefold_map.items():
        if k <= 0xFFFF:
            continue
        assert len(v) == 1
        casefold_table[k] = v[0]
    casefold_salt, casefold_keys = minimal_perfect_hash(casefold_table)
    writer.write(
        f"\nconst uint16_t NORMDATA_CASEFOLD_SALT[{len(casefold_salt)}] = {{\n"
    )
    for salts in batched(casefold_salt, 14):
        writer.write(" ")
        for s in salts:
            writer.write(f" 0x{s:04X},")
        writer.write("\n")
    writer.write("};\n")
    writer.write(
        f"\nconst uint32_t NORMDATA_CASEFOLD_KV[{len(casefold_keys)}][2] = {{\n"
    )
    for batch in batched(casefold_keys, 8):
        writer.write(" ")
        for k in batch:
            casefold = casefold_table[k]
            writer.write(f" {{0x{casefold:08X}, 0x{k:08X}}},")
        writer.write("\n")
    writer.write("};\n")

    headers.extend(
        [
            HeaderDef.array("NORMDATA_CASEFOLD_SALT", "uint16_t", len(casefold_salt)),
            HeaderDef.multi_array(
                "NORMDATA_CASEFOLD_KV", "uint32_t", [len(casefold_keys), 2]
            ),
        ]
    )

    return headers


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


def build_shuf(sizes: tuple[int, ...], size12: int) -> list[int]:
    answer = [0] * 16
    pos = 0
    if max(sizes) <= 2 and len(sizes) == size12:
        for i in range(size12):
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


def generate_shufutf8(writer, size12: int, suffix: str) -> list[HeaderDef]:
    case12_small_set: set[tuple[int, ...]] = set()
    case12_set: set[tuple[int, ...]] = set()
    case123_set: set[tuple[int, ...]] = set()
    case1234_set: set[tuple[int, ...]] = set()
    for x in range(1 << 12):
        sizes = compute_code_point_size(x)
        if has_code_points_up_to_size(sizes, size=2, n=size12):
            if sum(sizes[:size12]) <= 8:
                case12_small_set.add(tuple(sizes[:size12]))
            else:
                case12_set.add(tuple(sizes[:size12]))
        elif has_code_points_up_to_size(sizes, size=3, n=4):
            case123_set.add(tuple(sizes[:4]))
        elif has_code_points_up_to_size(sizes, size=4, n=3):
            case1234_set.add(tuple(sizes[:3]))
    case12_small = sorted(case12_small_set)
    case12 = sorted(case12_set)
    case123 = sorted(case123_set)
    case1234 = sorted(case1234_set)
    cases = case12_small + case12 + case123 + case1234

    all_shuf = [build_shuf(z, size12) for z in cases]
    writer.write(f"\nconst uint8_t NORMDATA_SHUFUTF8{suffix}[{len(cases)}][16] = {{\n")
    for shuf in all_shuf:
        writer.write(f"  {{{", ".join(map(str, shuf))}}},\n")
    writer.write("};\n")

    index = {t: i for i, t in enumerate(cases)}
    arrg = []
    for x in range(1 << 12):
        sizes = compute_code_point_size(x)
        if has_code_points_up_to_size(sizes, size=2, n=size12):
            idx = index[tuple(sizes[:size12])]
            arrg.append((idx, sum(sizes[:size12])))
        elif has_code_points_up_to_size(sizes, size=3, n=4):
            idx = index[tuple(sizes[:4])]
            arrg.append((idx, sum(sizes[:4])))
        elif has_code_points_up_to_size(sizes, size=4, n=3):
            idx = index[tuple(sizes[:3])]
            arrg.append((idx, sum(sizes[:3])))
        else:
            arrg.append((len(all_shuf), 12))

    writer.write(
        f"\nconst uint8_t NORMDATA_CODE_POINT_INDEX{suffix}[{len(arrg)}][2] = {{\n"
    )
    for row in batched(arrg, 8):
        writer.write(" ")
        for a in row:
            writer.write(f" {{{", ".join(map(str, a))}}},")
        writer.write("\n")
    writer.write("};\n")

    writer.write(
        f"\nconst uint8_t NORMDATA_SHUFUTF8{suffix}_INDEX_12_SMALL = {len(case12_small)};\n"
    )
    writer.write(
        f"const uint8_t NORMDATA_SHUFUTF8{suffix}_INDEX_12 = {len(case12_small) + len(case12)};\n"
    )
    writer.write(
        f"const uint8_t NORMDATA_SHUFUTF8{suffix}_INDEX_123 = {len(case12_small) + len(case12) + len(case123)};\n"
    )
    writer.write(
        f"const uint8_t NORMDATA_SHUFUTF8{suffix}_INDEX_1234 = {len(cases)};\n"
    )

    return [
        HeaderDef.multi_array(
            f"NORMDATA_SHUFUTF8{suffix}", "uint8_t", [len(cases), 16]
        ),
        HeaderDef.multi_array(
            f"NORMDATA_CODE_POINT_INDEX{suffix}", "uint8_t", [len(arrg), 2]
        ),
        HeaderDef(f"NORMDATA_SHUFUTF8{suffix}_INDEX_12_SMALL", "uint8_t"),
        HeaderDef(f"NORMDATA_SHUFUTF8{suffix}_INDEX_12", "uint8_t"),
        HeaderDef(f"NORMDATA_SHUFUTF8{suffix}_INDEX_123", "uint8_t"),
        HeaderDef(f"NORMDATA_SHUFUTF8{suffix}_INDEX_1234", "uint8_t"),
    ]


def generate_shuffle_tables(writer) -> list[HeaderDef]:
    headers = []
    headers.extend(generate_shufutf8(writer, size12=4, suffix=""))
    headers.extend(generate_shufutf8(writer, size12=6, suffix="_WIDE"))

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
    headers.append(HeaderDef.array("NORMDATA_HANGUL_SHUF", "NormdataHangulShuf", 16))

    return headers


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


def generate_bloom_filter(writer, name: str, bloom: BloomFilter) -> list[HeaderDef]:
    return [generate_array(writer, name, bloom.blocks, 32)]


def generate_trie(
    writer, name: str, trie: Trie, index_width: int, data_width: int
) -> list[HeaderDef]:
    return [
        generate_array(writer, name + "_INDEX", trie.index, index_width),
        generate_array(writer, name + "_DATA", trie.data, data_width),
    ]


def generate_header_def(writer, header_def: HeaderDef) -> None:
    array_info = "".join([f"[{x}]" for x in header_def.array_sizes])
    writer.write(f"extern const {header_def.type_} {header_def.name}{array_info};\n")


S_BASE = 0xAC00
L_BASE = 0x1100
V_BASE = 0x1161
T_BASE = 0x11A7
L_COUNT = 19
V_COUNT = 21
T_COUNT = 28
N_COUNT = V_COUNT * T_COUNT
S_COUNT = L_COUNT * N_COUNT


PREAMBLE_H = """// This file was generated by gen/gen.py

#ifndef XXUTF_NORMDATA_H
#define XXUTF_NORMDATA_H

#include <stdint.h>

typedef struct NormdataTableEntry {
  uint8_t len;
  uint8_t ccc;
  uint8_t last_ccc;
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
static const uint32_t NORMDATA_CASEFOLD_TABLE_SIZE = sizeof(NORMDATA_CASEFOLD_KV) / sizeof(uint64_t);

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

#endif // XXUTF_NORMDATA_H
"""

PREAMBLE = """// This file was generated by gen/gen.py

#include "normdata.h"

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


def create_decomp_trie(
    decomp_map: DecompMap, encoding: str, decomp_bound: int
) -> tuple[Trie, list[int]]:
    trie = Trie()
    data: list[int] = [0]
    for x in range(0x10000):
        try:
            size = len(chr(x).encode(encoding))
        except UnicodeEncodeError:
            continue
        if x not in decomp_map:
            trie.set(x, size)
            continue
        decomp = decomp_map[x]
        offset = len(data)
        # We use the lower 16 bits for the offset into the data table
        assert offset <= 0xFFFF
        for c in decomp.decomps:
            data.extend(chr(c).encode(encoding))
        length = len(data) - offset
        assert length <= decomp_bound
        if decomp.decomps[-1] in decomp_map:
            last_ccc = decomp_map[decomp.decomps[-1]].ccc
        else:
            last_ccc = 0
        starter = decomp_map[x].ccc == 0
        is_complex = False
        decomp_delta = length - len(chr(x).encode(encoding))
        if decomp_delta < 0 or decomp_delta > 0b111 or decomp_delta + size > 8:
            decomp_delta = 0
            is_complex = True
        value = (
            (int(is_complex) << 31)
            | (decomp_delta << 26)
            | (last_ccc << 18)
            | (offset << 2)
            | size
        )
        trie.set(x, value)
    trie.compact()
    return trie, data


def create_decomp_trie_2(
    decomp_map: DecompMap, encoding: str, decomp_bound: int
) -> tuple[Trie, Trie, list[int]]:
    trie = Trie()
    decomp_trie = Trie()
    data: list[int] = [0]
    for x in range(0x10000):
        try:
            size = len(chr(x).encode(encoding))
        except UnicodeEncodeError:
            continue
        if x not in decomp_map:
            trie.set(x, size)
            decomp_trie.set(x, 0)
            continue
        decomp = decomp_map[x]
        offset = len(data)
        # We use the lower 16 bits for the offset into the data table
        assert offset <= 0xFFFF
        for c in decomp.decomps:
            data.extend(chr(c).encode(encoding))
        length = len(data) - offset
        assert length <= decomp_bound
        decomp_delta = length - len(chr(x).encode(encoding))
        final_decomp = min(decomp_delta, 15)
        first_ccc = 0
        last_ccc = 0
        if decomp.decomps[-1] in decomp_map:
            last_ccc = decomp_map[decomp.decomps[-1]].ccc
            ccc_vals = [
                decomp_map.get(a, DecompValue([], 0)).ccc for a in decomp.decomps
            ]
            if (
                len(ccc_vals) > 1
                and any(ccc < last_ccc and ccc != 0 for ccc in ccc_vals)
                and ccc_vals[0] != 0
            ):
                first_ccc = ccc_vals[0]
                assert last_ccc - first_ccc in range(0, 8)
                final_decomp = 15
        # Delta decomposition can only be done with relatively small decomp
        # lengths (<= 8). A `final_decomp` value of 15 indicates that the
        # code point definitely cannot be delta decomposed, and thus the
        # `decomp_trie` trie should be used to get length information.
        if length > 8:
            final_decomp = 15
        value = (
            ((final_decomp & 0x1F) << 11)
            | (int(decomp.decomps[0] != x) << 10)
            | (last_ccc << 2)
            | size
        )
        trie.set(x, value)
        assert length <= 0x3F
        assert offset <= 0x7FFF
        ccc_delta = 0 if first_ccc == 0 else last_ccc - first_ccc
        decomp_trie.set(
            x,
            (ccc_delta << 29) | (last_ccc << 21) | (length << 15) | offset,
        )
    trie.compact()
    decomp_trie.compact()
    return trie, decomp_trie, data


def create_decomp_length_trie(decomp_map: DecompMap, encoding: str) -> Trie:
    trie = Trie()
    for x in range(0x10000):
        if x not in decomp_map:
            if x >= S_BASE and x < S_BASE + S_COUNT:
                # Handle Hangul syllables
                s_index = x - S_BASE
                jamo_size = len(chr(L_BASE).encode(encoding))
                trie.set(x, 2 * jamo_size if s_index % T_COUNT == 0 else 3 * jamo_size)
            else:
                try:
                    trie.set(x, len(chr(x).encode(encoding)))
                except UnicodeEncodeError:
                    # Python throws an error if we try to encode a surrogate
                    # in UTF-8
                    pass
            continue
        decomp = decomp_map[x]
        length = 0
        for c in decomp.decomps:
            length += len(chr(c).encode(encoding))
        trie.set(x, length)
    trie.compact()
    return trie


def create_comp_trie(
    qc: list[int],
    decomp_map: DecompMap,
    non_starters: list[int],
    composables: list[int],
) -> Trie:
    trie = Trie()
    for x in range(0x10000):
        if x in qc or x in non_starters:
            # This identifies a special but interesting class of characters that:
            # 1. Do not compose with anything
            # 2. Decompose into a single character
            # 3. The decomposed character does not compose with anything
            # Such characters are a subset of NF(K)C_QC that have nothing to
            # do with composition at all (they're only relevance is that they
            # can be decomposed). They get a special value in the trie so that
            # a potential optimization is available: if we have an input x with
            # code points with value 0 or this special 1 value (and no 2 values),
            # we have NF(K)D(x) == NF(K)C(x).
            if (
                x not in composables
                and len(decomp_map[x].decomps) == 1
                and decomp_map[x].decomps[0] not in composables
            ):
                value = 1
            else:
                value = 2
        else:
            value = 0
        trie.set(x, value)
    trie.compact()
    return trie


def create_comp_length_trie(
    qc: list[int], decomp_map: DecompMap, encoding: str
) -> Trie:
    trie = Trie()
    for x in range(0x10000):
        if x in qc and x in decomp_map:
            length = sum(len(chr(c).encode(encoding)) for c in decomp_map[x].decomps)
            trie.set(x, length)
        else:
            try:
                trie.set(x, len(chr(x).encode(encoding)))
            except UnicodeEncodeError:
                pass
    trie.compact()
    return trie


def load_casefold_map() -> CasefoldMap:
    map: CasefoldMap = {}

    with open("CaseFolding.txt", "r") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split(";")
            c = int(parts[0], 16)
            kind = parts[1].strip()
            mappings = [int(x, 16) for x in parts[2].strip().split(" ")]
            # Skip Turkic and simple mappings for now
            if kind == "T" or kind == "S":
                continue
            map[c] = mappings

    return map


def create_casefold_trie(map: CasefoldMap, encoding: str) -> tuple[Trie, list[int]]:
    trie = Trie()
    data: list[int] = []
    for x in range(0x10000):
        if x not in map:
            trie.set(x, 0)
            continue
        casefold = map[x]
        offset = len(data)
        # Lower 12 bits are used for the offset
        assert offset <= 2**12 - 1
        for c in casefold:
            data.extend(chr(c).encode(encoding))
        length = len(data) - offset
        assert length <= 2**4 - 1
        value = offset | (length << 12)
        trie.set(x, value)
    trie.compact()
    return trie, data


def create_casefold_length_trie(map: CasefoldMap, encoding: str) -> Trie:
    trie = Trie()
    for x in range(0x10000):
        if x not in map:
            try:
                trie.set(x, len(chr(x).encode(encoding)))
            except UnicodeEncodeError:
                # Python throws an error if we try to encode a surrogate
                # in UTF-8
                pass
            continue
        casefold = map[x]
        length = 0
        for c in casefold:
            length += len(chr(c).encode(encoding))
        trie.set(x, length)
    trie.compact()
    return trie


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
    casefold_map = load_casefold_map()

    non_starters = [x for x, decomp in nfd_map.items() if decomp.ccc > 0]

    comp_map: CompMap = {}
    # Tracks characters that compose
    composables: list[int] = []
    for x, decomp in nfd_map.items():
        if x in derived.comp_exclusions or decomp.decomps[0] == x:
            continue
        assert len(decomp.decomps) == 2
        composables.extend(decomp.decomps)
        comp_map[(decomp.decomps[0], decomp.decomps[1])] = x
    # Add Hangul V Jamo
    composables.extend(range(V_BASE, V_BASE + V_COUNT))
    # Add Hangul T Jamo
    composables.extend(range(T_BASE + 1, T_BASE + T_COUNT))

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

    default_hash_scheme = [
        xorshift_hash,
        lambda c: hash_32bit_fast(c ^ 0xDEADBEEF),
        hash_32bit_fast,
    ]
    nfc_bloom = BloomFilter(
        32768,
        multiply_shift_hash,
        default_hash_scheme,
        [x for x in derived.nfc_qc if x > 0xFFFF],
    )
    nfkc_bloom = BloomFilter(
        32768,
        multiply_shift_hash,
        default_hash_scheme,
        [x for x in derived.nfkc_qc if x > 0xFFFF],
    )
    non_starters_bloom = BloomFilter(
        32768,
        multiply_shift_hash,
        default_hash_scheme,
        non_starters,
    )
    utf8_nfd_trie, utf8_nfd_data_trie, utf8_nfd_data = create_decomp_trie_2(
        nfd_map, "UTF-8", decomp_bound=16
    )
    utf8_nfkd_trie, utf8_nfkd_data_trie, utf8_nfkd_data = create_decomp_trie_2(
        nfkd_map, "UTF-8", decomp_bound=48
    )
    utf16_nfd_trie, utf16_nfd_data = create_decomp_trie(
        nfd_map, "UTF-16LE", decomp_bound=16
    )
    utf16_nfkd_trie, utf16_nfkd_data = create_decomp_trie(
        nfkd_map,
        "UTF-16LE",
        decomp_bound=48,
    )
    ccc_trie = Trie()
    for x in range(0x10000):
        if x in nfd_map:
            ccc_trie.set(x, nfd_map[x].ccc)
        else:
            ccc_trie.set(x, 0)
    ccc_trie.compact()
    casefold_utf8_trie, casefold_utf8_data = create_casefold_trie(casefold_map, "UTF-8")
    casefold_utf16_trie, casefold_utf16_data = create_casefold_trie(
        casefold_map, "UTF-16LE"
    )
    nfc_trie = create_comp_trie(derived.nfc_qc, nfd_map, non_starters, composables)
    nfkc_trie = create_comp_trie(derived.nfkc_qc, nfkd_map, non_starters, composables)
    utf8_nfd_length_trie = create_decomp_length_trie(nfd_map, "UTF-8")
    utf8_nfkd_length_trie = create_decomp_length_trie(nfkd_map, "UTF-8")
    utf8_casefold_length_trie = create_casefold_length_trie(casefold_map, "UTF-8")
    utf16_nfd_length_trie = create_decomp_length_trie(nfd_map, "UTF-16LE")
    utf16_nfkd_length_trie = create_decomp_length_trie(nfkd_map, "UTF-16LE")
    utf16_casefold_length_trie = create_casefold_length_trie(casefold_map, "UTF-16LE")
    utf8_nfc_length_trie = create_comp_length_trie(derived.nfc_qc, nfd_map, "UTF-8")
    utf8_nfkc_length_trie = create_comp_length_trie(derived.nfkc_qc, nfkd_map, "UTF-8")
    utf16_nfc_length_trie = create_comp_length_trie(derived.nfc_qc, nfd_map, "UTF-16LE")
    utf16_nfkc_length_trie = create_comp_length_trie(
        derived.nfkc_qc, nfkd_map, "UTF-16LE"
    )

    headers: list[HeaderDef] = []
    with open("normdata.c", "w") as f:
        f.write(PREAMBLE)
        headers.extend(
            generate_hash_tables(f, nfd_map, nfkd_map, comp_map, casefold_map)
        )
        headers.extend(generate_shuffle_tables(f))
        headers.append(
            generate_array(f, "NORMDATA_UTF8_NFD_TRIE_DECOMPOSITIONS", utf8_nfd_data, 8)
        )
        headers.append(
            generate_array(
                f, "NORMDATA_UTF8_NFKD_TRIE_DECOMPOSITIONS", utf8_nfkd_data, 8
            )
        )
        headers.extend(
            generate_trie(f, "NORMDATA_UTF8_NFD_TRIE", utf8_nfd_trie, 16, 16)
        )
        headers.extend(
            generate_trie(f, "NORMDATA_UTF8_NFD_DATA_TRIE", utf8_nfd_data_trie, 16, 32)
        )
        headers.extend(
            generate_trie(f, "NORMDATA_UTF8_NFKD_TRIE", utf8_nfkd_trie, 16, 16)
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF8_NFKD_DATA_TRIE", utf8_nfkd_data_trie, 16, 32
            )
        )
        headers.append(
            generate_array(
                f, "NORMDATA_UTF16_NFD_TRIE_DECOMPOSITIONS", utf16_nfd_data, 8
            )
        )
        headers.append(
            generate_array(
                f, "NORMDATA_UTF16_NFKD_TRIE_DECOMPOSITIONS", utf16_nfkd_data, 8
            )
        )
        headers.extend(
            generate_trie(f, "NORMDATA_UTF16_NFD_TRIE", utf16_nfd_trie, 16, 32)
        )
        headers.extend(
            generate_trie(f, "NORMDATA_UTF16_NFKD_TRIE", utf16_nfkd_trie, 16, 32)
        )
        headers.extend(generate_trie(f, "NORMDATA_CCC_TRIE", ccc_trie, 16, 8))
        headers.extend(generate_trie(f, "NORMDATA_NFC_TRIE", nfc_trie, 16, 8))
        headers.extend(generate_trie(f, "NORMDATA_NFKC_TRIE", nfkc_trie, 16, 8))
        headers.extend(generate_bloom_filter(f, "NORMDATA_NFC_BLOOM_FILTER", nfc_bloom))
        headers.extend(
            generate_bloom_filter(f, "NORMDATA_NFKC_BLOOM_FILTER", nfkc_bloom)
        )
        headers.extend(
            generate_bloom_filter(
                f, "NORMDATA_NON_STARTERS_BLOOM_FILTER", non_starters_bloom
            )
        )
        headers.append(
            generate_array(f, "NORMDATA_UTF8_CASEFOLD_DATA", casefold_utf8_data, 8)
        )
        headers.extend(
            generate_trie(f, "NORMDATA_UTF8_CASEFOLD_TRIE", casefold_utf8_trie, 16, 16)
        )
        headers.append(
            generate_array(f, "NORMDATA_UTF16_CASEFOLD_DATA", casefold_utf16_data, 8)
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF16_CASEFOLD_TRIE", casefold_utf16_trie, 16, 16
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF8_NFD_LENGTH_TRIE", utf8_nfd_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF8_NFKD_LENGTH_TRIE", utf8_nfkd_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f,
                "NORMDATA_UTF8_CASEFOLD_LENGTH_TRIE",
                utf8_casefold_length_trie,
                16,
                8,
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF16_NFD_LENGTH_TRIE", utf16_nfd_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF16_NFKD_LENGTH_TRIE", utf16_nfkd_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f,
                "NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE",
                utf16_casefold_length_trie,
                16,
                8,
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF8_NFC_LENGTH_TRIE", utf8_nfc_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF8_NFKC_LENGTH_TRIE", utf8_nfkc_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF16_NFC_LENGTH_TRIE", utf16_nfc_length_trie, 16, 8
            )
        )
        headers.extend(
            generate_trie(
                f, "NORMDATA_UTF16_NFKC_LENGTH_TRIE", utf16_nfkc_length_trie, 16, 8
            )
        )
    with open("normdata.h", "w") as f:
        f.write(PREAMBLE_H)
        for header in headers:
            generate_header_def(f, header)
        f.write(POSTAMBLE_H)

    lines: list[str] = []
    ELEMENT_SIZES = {
        "uint8_t": 1,
        "uint16_t": 2,
        "uint32_t": 4,
        "uint64_t": 8,
        "NormdataTableEntry": 12,
        "NormdataHangulShuf": 25,
    }
    KILOBYTE = 1024
    total = 0
    for header in headers:
        if not header.array_sizes:
            continue
        size = header.size(ELEMENT_SIZES[header.type_])
        lines.append(f"{header.name}: {size / KILOBYTE:.1f}KiB")
        total += size
    lines.append(f"TOTAL: {total / KILOBYTE:.1f}KiB")
    aligned = align_key_value_lines(lines)
    for line in aligned:
        print(line)


if __name__ == "__main__":
    main()
