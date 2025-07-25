#!/usr/bin/env python3

import argparse
import re
import sys


SOURCES = ["utf8norm.c", "normdata.c", "impl/scalar.c", "impl/neon.c"]
PREAMBLE = """/*
This file is an amalgamation of all the C source files in the utf8norm codebase.
This is the only file you need to compile utf8norm, and compilation will be done
within a single unit.

See: https://www.sqlite.org/amalgamation.html
*/

"""


include_re = re.compile(r"^#include \"(.*)\"")
sys_include_re = re.compile(r"^#include <(.*)>")
ifdef_cpp_re = re.compile(r"^#ifdef __cplusplus")
decl_re = re.compile(r"([_a-zA-Z][_a-zA-Z0-9 ]+ \**)([_a-zA-Z0-9]+)\((.*)")
var_re = re.compile(r"^([_a-zA-Z][a-zA-Z_0-9 ]+ \**)([_a-zA-Z0-9]+)(\[|;| =)")



def copy_file(out, file: str, seen_headers: set[str]) -> None:
    out.write(f"/*amalgamate: BEGIN {file}*/\n")
    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            if (include_match := include_re.match(line)) is not None:
                include = include_match.group(1)
                if include not in seen_headers:
                    copy_file(out, include, seen_headers)
                    seen_headers.add(include)
                else:
                    out.write(f"/*amalgamate: skip {line.strip()}*/\n")
            elif (sys_include_match := sys_include_re.match(line)) is not None:
                sys_include = sys_include_match.group(1)
                if sys_include not in seen_headers:
                    out.write(line)
                    seen_headers.add(sys_include)
                else:
                    out.write(f"/*amalgamate: skip {line.strip()}*/\n")
            elif (decl_match := decl_re.match(line)) is not None:
                name = decl_match.group(2)
                if not line.startswith("static") and not name.startswith("utf8norm_"):
                    out.write(f"static {line}")
                else:
                    out.write(line)
            elif (var_match := var_re.match(line)) is not None:
                name = var_match.group(2)
                if not line.startswith("static") and not name.lower().startswith("utf8norm_"):
                    if line.startswith("extern"):
                        out.write(f"static {line[len("extern "):]}")
                    else:
                        out.write(f"static {line}")
                else:
                    out.write(line)
            elif ifdef_cpp_re.match(line) is not None:
                out.write("#if 0\n")
            else:
                out.write(line)
    out.write(f"/*amalgamate: END {file}*/\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="amalgamate",
        description="Amalgamate the entire utf8norm codebase into one file",
    )
    parser.add_argument("-o", "--output", type=str, help="output file (default: stdout)")
    
    args = parser.parse_args()

    seen_headers: set[str] = set()
    if args.output is None:
        sys.stdout.write(PREAMBLE)
        for file in SOURCES:
            copy_file(sys.stdout, file, seen_headers)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(PREAMBLE)
            for file in SOURCES:
                copy_file(f, file, seen_headers)


if __name__ == "__main__":
    main()
