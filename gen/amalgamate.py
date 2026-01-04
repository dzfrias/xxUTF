#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


PREAMBLE = """/*
This file is an amalgamation of all the C source files in the xxUTF codebase.
This is the only file you need to compile xxUTF, and compilation will be done
within a single unit.

See: https://www.sqlite.org/amalgamation.html
*/

"""


include_re = re.compile(r"^#include \"(.*)\"")
ifdef_cpp_re = re.compile(r"^#ifdef __cplusplus")
decl_re = re.compile(r"([_a-zA-Z][_a-zA-Z0-9 ]+ \**)([_a-zA-Z0-9]+)\((.*)")
var_re = re.compile(r"^([_a-zA-Z][a-zA-Z_0-9 ]+ \**)([_a-zA-Z0-9]+)(\[|;| =)")
add_re = re.compile(r"^// amalgamate add: (.*)")


def copy_file(out, file: Path, seen_headers: set[str]) -> None:
    out.write(f"/*amalgamate: BEGIN {file}*/\n")
    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            if (include_match := include_re.match(line)) is not None:
                include = include_match.group(1)
                if include not in seen_headers:
                    copy_file(out, Path(include), seen_headers)
                    seen_headers.add(include)
                else:
                    out.write(f"/*amalgamate: skip {line.strip()}*/\n")
            elif (decl_match := decl_re.match(line)) is not None:
                name = decl_match.group(2)
                if not line.startswith("static") and not name.startswith("xxutf_"):
                    out.write(f"__attribute__((unused)) static {line}")
                else:
                    out.write(line)
            elif (var_match := var_re.match(line)) is not None:
                name = var_match.group(2)
                if not name.lower().startswith("xxutf_"):
                    if not line.startswith("static"):
                        if line.startswith("extern"):
                            line = line[len("extern "):]
                        out.write(f"__attribute__((unused)) static {line}")
                    else:
                        out.write(f"__attribute__((unused)) {line}")
                else:
                    out.write(line)
            elif (add_match := add_re.match(line)) is not None:
                stmt = add_match.group(1)
                out.write(stmt + "\n")
            elif ifdef_cpp_re.match(line) is not None:
                out.write("#if 0\n")
            else:
                out.write(line)
    out.write(f"/*amalgamate: END {file}*/\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="amalgamate",
        description="Amalgamate the entire xxUTF codebase into one file",
    )
    parser.add_argument("sources", help="directory with the xxUTF codebase", nargs="+")
    parser.add_argument(
        "-o", "--output", type=str, help="output file (default: stdout)"
    )

    args = parser.parse_args()

    cwd = Path.cwd()
    seen_headers: set[str] = set()
    if args.output is None:
        sys.stdout.write(PREAMBLE)
        for file in args.sources:
            copy_file(sys.stdout, file, seen_headers)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(PREAMBLE)
            for file in args.sources:
                file_path = Path(file)
                copy_file(f, file_path.relative_to(cwd), seen_headers)


if __name__ == "__main__":
    main()
