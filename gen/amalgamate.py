#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path
from typing import Union


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


def find_header(name: str, includes: list[Path]) -> Union[Path, None]:
    for dir in includes:
        full_path = dir / name
        if full_path.exists():
            return full_path
    return None


def copy_file(
    out, file: Path, seen_headers: set[str], include_dirs: list[Path]
) -> None:
    out.write(f"/*amalgamate: BEGIN {file.name}*/\n")
    with open(file, "r", encoding="utf-8") as f:
        for line in f:
            if (include_match := include_re.match(line)) is not None:
                include = include_match.group(1)
                if include not in seen_headers:
                    header = find_header(include, include_dirs)
                    if header is not None:
                        copy_file(
                            out,
                            header,
                            seen_headers,
                            include_dirs,
                        )
                        seen_headers.add(include)
                else:
                    out.write(f"/*amalgamate: skip {line.strip()}*/\n")
            elif (decl_match := decl_re.match(line)) is not None:
                name = decl_match.group(2)
                if not line.startswith("static") and not name.startswith("xxutf_"):
                    out.write(f"XXUTF_UNUSED static {line}")
                else:
                    out.write(line)
            elif (var_match := var_re.match(line)) is not None:
                name = var_match.group(2)
                if not name.lower().startswith("xxutf_"):
                    if not line.startswith("static"):
                        if not line.startswith("extern"):
                            out.write(f"XXUTF_UNUSED static {line}")
                    else:
                        out.write(f"XXUTF_UNUSED {line}")
                else:
                    out.write(line)
            elif (add_match := add_re.match(line)) is not None:
                stmt = add_match.group(1)
                out.write(stmt + "\n")
            elif ifdef_cpp_re.match(line) is not None:
                out.write("#if 0\n")
            else:
                out.write(line)
    out.write(f"/*amalgamate: END {file.name}*/\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="amalgamate",
        description="Amalgamate the entire xxUTF codebase into one file",
    )
    parser.add_argument(
        "sources", type=Path, help="directory with the xxUTF codebase", nargs="+"
    )
    parser.add_argument(
        "-o", "--output", type=str, help="output file (default: stdout)"
    )
    parser.add_argument(
        "--common-defs",
        type=Path,
        help="path to common_defs.h file",
        required=True,
    )
    parser.add_argument(
        "-I", "--include", type=Path, help="header file sources", nargs="*"
    )

    args = parser.parse_args()

    seen_headers: set[str] = set()
    seen_headers.add(args.common_defs.name)
    if args.output is None:
        sys.stdout.write(PREAMBLE)
        # Include common_defs first because we use XXUTF_UNUSED in this script
        copy_file(sys.stdout, args.common_defs, seen_headers, args.include)
        for file in args.sources:
            copy_file(sys.stdout, Path(file), seen_headers, args.include)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(PREAMBLE)
            copy_file(f, args.common_defs, seen_headers, args.include)
            for file in args.sources:
                file_path = Path(file)
                copy_file(f, file_path, seen_headers, args.include)


if __name__ == "__main__":
    main()
