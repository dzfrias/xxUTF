"""
Module to build a 2-stage trie made for Unicode code points in the BMP.
Supports compaction.

The design and compaction techniques used are inspired by ICU4C's
UCPTrie.
"""

BMP_LIMIT = 0x10000

SHIFT = 6
BLOCK_LENGTH = 1 << SHIFT
MASK = BLOCK_LENGTH - 1

FLAG_UNSEEN = 0
FLAG_SEEN = 1


class Trie:
    def __init__(self) -> None:
        self.index: list[int] = [0] * (BMP_LIMIT >> SHIFT)
        self.data: list[int] = []
        self._flags: list[int] = [FLAG_UNSEEN] * (BMP_LIMIT >> SHIFT)

    def get(self, c: int) -> int:
        i = c >> SHIFT
        return self.data[self.index[i] + (c & MASK)]

    def set(self, c: int, value: int) -> None:
        assert c < BMP_LIMIT
        i = c >> SHIFT
        block: int
        # Already exists as an index
        if self._flags[i] == FLAG_SEEN:
            block = self.index[i]
        else:
            block = self._alloc_new_block(i)
        self.data[block + (c & MASK)] = value

    def compact(self) -> None:
        compressed = []

        # Keeps track of blocks that we've seen before
        blocks = {}
        for i in range(len(self.index)):
            block_index = self.index[i]
            block_slice = self.data[block_index : block_index + BLOCK_LENGTH]
            block = tuple(block_slice)
            # Check if we can fully deduplicate this block
            if block in blocks:
                self.index[i] = blocks[block]
                continue

            # If we can't deduplicate the block, then see if we can merge it
            # partially with the previous block
            overlap = longest_overlap(compressed, block_slice, BLOCK_LENGTH)
            if overlap > 0:
                index = len(compressed) - overlap
                compressed.extend(block_slice[overlap:])
                self.index[i] = index
                blocks[block] = index
                continue

            # If we can't do either of the above compaction operations, we
            # should allocate the new data
            self.index[i] = len(compressed)
            blocks[block] = len(compressed)
            compressed.extend(block)

        self.data = compressed

    def _alloc_new_block(self, i: int) -> int:
        new_block = len(self.data)
        self.data.extend([0] * BLOCK_LENGTH)
        self.index[i] = new_block
        self._flags[i] = FLAG_SEEN
        return new_block


def longest_overlap(lst1: list, lst2: list, max_overlap: int) -> int:
    overlap = max_overlap - 1
    if overlap > len(lst1):
        return 0
    while overlap > 0 and lst1[-overlap:] != lst2[:overlap]:
        overlap -= 1
    return overlap
