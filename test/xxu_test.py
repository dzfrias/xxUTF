#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path

ALGORITHMS = ["nfd", "nfkd"]

xxu_bin = sys.argv[1]

for algorithm in ALGORITHMS:
    for input_dir in sys.argv[2:]:
        for file in Path(input_dir).iterdir():
            xxu_result = subprocess.run(
                [xxu_bin, "-x", algorithm, str(file)], capture_output=True
            )
            if xxu_result.returncode != 0:
                print(f"got exit code {xxu_result.returncode} for input {str(file)}")
                sys.exit(1)
            uconv_result = subprocess.run(
                ["uconv", "-x", f"any-{algorithm}", str(file)], capture_output=True
            )
            assert uconv_result.returncode == 0
            if xxu_result.stdout != uconv_result.stdout:
                print(f"got output mismatch for input {str(file)}")
                sys.exit(1)

print("All xxu tests passed!")
