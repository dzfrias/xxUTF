#!/usr/bin/env python

from itertools import batched

DecompMap = dict[int, list[int]]


# Helper to recusively decompose a Unicode character `c`
def expand(c: int, map: DecompMap) -> list[int]:
    expansion = []
    stack = [c]
    while stack:
        x = stack.pop()
        if x not in map or not map[x]:
            expansion.append(x)
        elif map[x]:
            stack.extend(map[x])

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


PREAMBLE = """typedef struct Entry {
  uint32_t k;
  uint16_t v1;
  uint16_t v2;
} Entry;

"""


def generate(writer, decomp_map: DecompMap) -> None:
    writer.write(PREAMBLE)

    offsets = {}
    offset = 0
    writer.write("static const uint32_t DECOMPOSED_CHARS[] = {\n")
    for k, decomp in decomp_map.items():
        offsets[k] = offset
        offset += len(decomp)
        for c in decomp:
            writer.write(f"    0x{c:04X},\n")
    writer.write("};\n")

    salt, keys = minimal_perfect_hash(decomp_map)
    writer.write("\nstatic const uint16_t DECOMPOSED_SALT[] = {\n")
    for salts in batched(salt, 14):
        writer.write(" " * 3)
        for s in salts:
            writer.write(f" 0x{s:04X},")
        writer.write("\n")
    writer.write("};\n")

    writer.write("\nstatic const Entry DECOMPOSED_KV[] = {\n")
    for batch in batched(keys, 5):
        writer.write(" " * 3)
        for k in batch:
            writer.write(
                f" {{0x{k:05X}, 0x{offsets[k]:03X}, 0x{len(decomp_map[k]):X}}},"
            )
        writer.write("\n")
    writer.write("};\n")


def utf8_bytes_needed(n: int) -> int:
    if n <= 0x7F:
        return 1
    elif n <= 0x7FF:
        return 2
    elif n <= 0xFFFF:
        return 3
    elif n <= 0x10FFFF:
        return 4

    raise ValueError()


def main() -> None:
    decomp_map: DecompMap = {}

    # Read in UnicodeData.txt and generate a decomposition mapping
    with open("UnicodeData.txt", "r") as f:
        for line in f:
            info = line.split(";")
            value = int(info[0], 16)
            mappings = info[5].split(" ")

            # Skip decomp if there is nothing or if it is a compatibility decomposition
            if mappings[0] == "" or mappings[0].startswith("<"):
                continue

            decomp_map[value] = [int(x, 16) for x in mappings]

    # Get the full expansion of each code point
    for x, decomp in decomp_map.items():
        final_decomp: list[int] = []
        for c in decomp:
            # Get the full expansion of each code point that makes up `x`
            expansion = expand(c, decomp_map)
            final_decomp.extend(expansion)
        # TODO: sort this into proper order (by CCC)
        decomp_map[x] = final_decomp

    with open("normdata.c", "w") as f:
        generate(f, decomp_map)


if __name__ == "__main__":
    main()
