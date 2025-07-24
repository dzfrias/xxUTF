#!/usr/bin/env python3

from itertools import batched
from dataclasses import dataclass


@dataclass
class DecompValue:
    decomps: list[int]
    ccc: int


DecompMap = dict[int, DecompValue]
CCCMap = dict[int, int]


# Helper to recusively decompose a Unicode character `c`
def expand(c: int, map: DecompMap) -> list[int]:
    expansion = []
    stack = [c]
    while stack:
        x = stack.pop()
        if x not in map or not map[x].decomps:
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
    decomp_bytes_len: int
    salt_len: int
    kv_len: int


def generate_hash_tables(writer, decomp_map: DecompMap) -> TableInfo:
    offsets = {}
    lengths = {}
    offset = 0
    all_decomp_bytes = []
    for k, decomp in decomp_map.items():
        if not decomp.decomps:
            continue
        offsets[k] = offset
        decomp_bytes: list[int] = []
        for c in decomp.decomps:
            utf8 = bytes(chr(c), encoding="UTF-8")
            decomp_bytes.extend(list(utf8))
        all_decomp_bytes.extend(decomp_bytes)
        lengths[k] = len(decomp_bytes)
        offset += len(decomp_bytes)
    assert offset <= 2**16 - 1

    writer.write(f"const uint8_t DECOMPOSED_CHARS[{len(all_decomp_bytes)}] = {{\n")
    for row in batched(all_decomp_bytes, 13):
        writer.write(" ");
        for b in row:
            writer.write(f" 0x{b:02X},")
        writer.write("\n");
    writer.write("};\n")
    assert offset <= 2**16 - 1

    salt, keys = minimal_perfect_hash(decomp_map)
    writer.write(f"\nconst uint16_t DECOMPOSED_SALT[{len(salt)}] = {{\n")
    for salts in batched(salt, 14):
        writer.write(" ")
        for s in salts:
            writer.write(f" 0x{s:04X},")
        writer.write("\n")
    writer.write("};\n")

    writer.write(f"\nconst Entry DECOMPOSED_KV[{len(keys)}] = {{\n")
    for batch in batched(keys, 5):
        writer.write(" ")
        for k in batch:
            ccc = decomp_map[k].ccc
            writer.write(
                f" {{{lengths.get(k, 0)}, {ccc}, 0x{offsets.get(k, 0):03X}, 0x{k:05X}}},"
            )
        writer.write("\n")
    writer.write("};\n")

    return TableInfo(decomp_bytes_len=len(all_decomp_bytes), salt_len=len(salt), kv_len=len(keys))


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


def has_code_points_up_to_size(sizes: list[int], size: int) -> bool:
    if len(sizes) < (12 // size):
        return False
    return max(sizes[:(12 // size)]) <= size


def build_shuf(sizes: tuple[int, ...]) -> list[int]:
    answer = [0] * 16
    pos = 0
    if len(sizes) == 6:
        for i in range(6):
            if sizes[i] == 1:
                answer[i * 2] = pos
                answer[i * 2 + 1] = 0xff
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
        if has_code_points_up_to_size(sizes, 2):
            case12_set.add(tuple(sizes[:6]))
        elif has_code_points_up_to_size(sizes, 3):
            case123_set.add(tuple(sizes[:4]))
        elif has_code_points_up_to_size(sizes, 4):
            case1234_set.add(tuple(sizes[:3]))
    case12 = sorted(case12_set)
    case123 = sorted(case123_set)
    case1234 = sorted(case1234_set)
    cases = case12 + case123 + case1234
  
    all_shuf = [build_shuf(z) for z in cases]
    writer.write(f"\nconst uint8_t SHUFUTF8[{len(cases)}][16] = {{\n")
    for shuf in all_shuf:
        writer.write(f"  {{{", ".join(map(str, shuf))}}},\n")
    writer.write("};\n")

    index = {t: i for i, t in enumerate(cases)}
    arrg = []
    for x in range(1 << 12):
        sizes = compute_code_point_size(x)
        if has_code_points_up_to_size(sizes, 2):
            idx = index[tuple(sizes[:6])]
            arrg.append((idx, sum(sizes[:6])))
        elif has_code_points_up_to_size(sizes, 3):
            idx = index[tuple(sizes[:4])]
            arrg.append((idx, sum(sizes[:4])))
        elif has_code_points_up_to_size(sizes, 4):
            idx = index[tuple(sizes[:3])]
            arrg.append((idx, sum(sizes[:3])))
        else:
            # We are in error, use a bogus index
            arrg.append((209, 12))

    writer.write(f"\nconst uint8_t CODEPOINT_INDEX[{len(arrg)}][2] = {{\n")
    for row in batched(arrg, 8):
        writer.write(" ");
        for a in row:
            writer.write(f" {{{", ".join(map(str, a))}}},")
        writer.write("\n")
    writer.write("};\n");

    writer.write(f"\nconst HangulShuf HANGUL_SHUF[16] = {{\n")
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
    writer.write("};\n");

    return ShuffleInfo(shufutf8_len=len(cases), codepoint_index_len=len(arrg))


def generate_header(writer, table_info: TableInfo, shuf_info: ShuffleInfo):
    writer.write(f"extern const uint8_t DECOMPOSED_CHARS[{table_info.decomp_bytes_len}];\n")
    writer.write(f"extern const uint16_t DECOMPOSED_SALT[{table_info.salt_len}];\n")
    writer.write(f"extern const Entry DECOMPOSED_KV[{table_info.kv_len}];\n")
    writer.write(f"extern const uint8_t SHUFUTF8[{shuf_info.shufutf8_len}][16];\n")
    writer.write(f"extern const uint8_t CODEPOINT_INDEX[{shuf_info.codepoint_index_len}][2];\n")
    writer.write(f"extern const HangulShuf HANGUL_SHUF[16];\n")


PREAMBLE_H = """// This file was generated by gen/gen.py

#ifndef UTF8NORM_NORMDATA_H
#define UTF8NORM_NORMDATA_H

#include <stdint.h>

typedef struct Entry {
  uint8_t len;
  uint8_t ccc;
  uint16_t offset;
  uint32_t k;
} Entry;

typedef struct HangulShuf {
  uint8_t len;
  uint8_t tbl[24];
} HangulShuf;

static const uint16_t S_BASE = 0xAC00;
static const uint16_t L_BASE = 0x1100;
static const uint16_t V_BASE = 0x1161;
static const uint16_t T_BASE = 0x11A7;
static const uint16_t L_COUNT = 19;
static const uint16_t V_COUNT = 21;
static const uint16_t T_COUNT = 28;
static const uint16_t N_COUNT = V_COUNT * T_COUNT;
static const uint16_t S_COUNT = L_COUNT * N_COUNT;

"""

POSTAMBLE_H = """
static const uint32_t DECOMPOSED_SALT_SIZE = sizeof(DECOMPOSED_SALT) / 2;
static const uint32_t DECOMPOSED_KV_SIZE = sizeof(DECOMPOSED_KV) / sizeof(Entry);
static const uint32_t DECOMPOSED_CHARS_SIZE = sizeof(DECOMPOSED_CHARS);

#endif // UTF8NORM_NORMDATA_H
"""

PREAMBLE = """// This file was generated by gen/gen.py

#include "normdata.h"

"""

def main() -> None:
    decomp_map: DecompMap = {}
    ccc_map: CCCMap = {}

    # Read in UnicodeData.txt and generate a decomposition mapping
    with open("UnicodeData.txt", "r") as f:
        for line in f:
            info = line.split(";")
            value = int(info[0], 16)
            mappings = info[5].split(" ")
            ccc = int(info[3])
            
            if ccc > 0:
                # Add all CCC > 0 characters to the decomp map
                decomp_map[value] = DecompValue([], ccc)
                ccc_map[value] = ccc

            # Skip decomp if there is nothing or if it is a compatibility decomposition
            if mappings[0] == "" or mappings[0].startswith("<"):
                continue

            decomp_map[value] = DecompValue([int(x, 16) for x in mappings], ccc)

    # Get the full expansion of each code point
    max_decomps = 0
    for x, decomp in decomp_map.items():
        final_decomp: list[int] = []
        for c in decomp.decomps:
            # Get the full expansion of each code point that makes up `x`
            expansion = expand(c, decomp_map)
            final_decomp.extend(expansion)
        s = sorted(final_decomp, key=lambda c: ccc_map.get(c, 0))
        if s != final_decomp:
            print("Unsorted decomp detected. Re-check data")
            exit(1)
        decomp_map[x].decomps = final_decomp
        max_decomps = max(max_decomps, len(final_decomp))

    # It is a generally good assumption that precomposed code points don't decompose into 
    # more than four code points.
    assert max_decomps <= 4

    with open("normdata.c", "w") as f:
        f.write(PREAMBLE)
        hash_info = generate_hash_tables(f, decomp_map)
        shuf_info = generate_shuffle_tables(f)
    with open("normdata.h", "w") as f:
        f.write(PREAMBLE_H)
        generate_header(f, hash_info, shuf_info);
        f.write(POSTAMBLE_H)


if __name__ == "__main__":
    main()
