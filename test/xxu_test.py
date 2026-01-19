#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


ALGORITHMS = ["nfd", "nfkd", "nfc", "nfkc"]


def compare(xxu_bin, algorithm, name, contents):
    xxu_result = subprocess.run(
        [xxu_bin, "-x", algorithm], capture_output=True, input=contents
    )
    if xxu_result.returncode != 0:
        print(
            f"got exit code {xxu_result.returncode} for input {name} (algorithm: {algorithm})"
        )
        sys.exit(1)
    uconv_result = subprocess.run(
        ["uconv", "-x", f"any-{algorithm}"], capture_output=True, input=contents
    )
    assert uconv_result.returncode == 0
    if xxu_result.stdout != uconv_result.stdout:
        print(f"got output mismatch for input {name} (algorithm: {algorithm})")
        sys.exit(1)


xxu_bin = sys.argv[1]

for input_dir in sys.argv[2:]:
    for file in Path(input_dir).iterdir():
        contents = file.read_bytes()
        for algorithm in ALGORITHMS:
            name = str(file)
            compare(xxu_bin, algorithm, name, contents)
            if len(contents) < 256:
                extended = contents + b"0" * (256 - len(contents))
                compare(xxu_bin, algorithm, f"EXTENDED-{name}", extended)

print("All xxu tests passed!")
