# xxUTF

xxUTF is a C library that implements
[Unicode](https://en.wikipedia.org/wiki/Unicode) text transformation algorithms
at speed using SIMD. Current algorithms supported:

- NFD normalization
- NFC normalization
- NFKD normalization
- NFKC normalization
- Casefolding

All algorithms are compatible with UTF-8, UTF-16LE (little endian), and UTF-16BE
(big endian). Further helper functions are defined for efficient and correct
streaming versions of these algorithms. See the [API](#api) for details.

xxUTF never allocates memory, does not depend on libc, cannot fail, and has the
fastest open source implementations of the listed algorithms available. All
functions are comprehensively tested using both the available Unicode test
suites and a fuzzer.

## Usage

xxUTF is distributed as: a single C source file that is amalgamated before
build, and a single header file. This is similar to what
[SQLite does](https://sqlite.org/amalgamation.html).

Example C program utilizing xxUTF:

```c
#include <xxutf.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h.>

int main() {
  const char *s = "Ȁ character that needs to be decomposed";
  size_t length = strlen(s);
  printf("Old length: %zu\n", length);
  char out[64];
  size_t out_len = xxutf_normalize_utf8_nfd(s, strlen(s), out);
  printf("New length: %zu\n", out_len);
  return 0;
}
```

One major goal of xxUTF is to have the simplest, most predictable API surface as
possible. As such, one function call usually suffices for the core
functionality. More advanced cases, such as streaming normalization, have a more
involved API. See [Streaming](#streaming) specifics.

## API

All functions expect valid Unicode-encoded bytes as input. There are many
libraries that can implement Unicode validation at extremely fast speeds; it is
recommended to use one of those for the best safety and performance.

### Streaming

The streaming versions of some Unicode algorithms can usually be implemented
naively (such as with casefolding). However, not all algorithms have such nice
properties.

Mainly, normalizing text in a streaming manner requires some care. The problem
is that string concatenation is not closed under normalized forms. In other
words:

toNFD(x) + toNFD(y) = toNFD(x + y)

**does not** hold for all Unicode strings x and y. Read the Unicode
normalization specification for more details.

xxUTF thus has special API's so that streaming normalization can be implemented
in a non-allocating, efficient way.

## Benchmarks

xxUTF is benchmarked using a variety of large real-world inputs from multiple
languages. As there are many factors to consider during benchmarking, curious
users are encouraged to run the benchmark suite (or write their own benchmarks)
on their machines.

These are the results for running NFD normalization on UTF-8. Inputs vary in
size and complexity, so cross-input comparison is not meaningful here.

<img src="doc/neon_utf8_nfd.png" width="70%" />

Benchmarks are compared against the ICU4C library. Other libraries, like
[utf8proc](https://github.com/JuliaStrings/utf8proc), are not as fast as ICU4C.

## License

xxUTF is licenced under the MIT license.
