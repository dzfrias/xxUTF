#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


ALGORITHMS = ["nfd", "nfkd", "nfc", "nfkc"]
ENCODINGS = ["utf-8", "utf-16le"]


def compare(xxu_bin, algorithm, name, contents, encoding):
    xxu_result = subprocess.run(
        [xxu_bin, "--algorithm", algorithm, "--encoding", encoding.replace("-", "")],
        capture_output=True,
        input=contents,
    )
    if xxu_result.returncode != 0:
        print(
            f"got xxu exit code {xxu_result.returncode} for input {name} (algorithm: {algorithm}, encoding: {encoding})"
        )
        print(xxu_result.stderr.decode("UTF-8"))
        sys.exit(1)
    uconv_result = subprocess.run(
        [
            "uconv",
            "-x",
            f"any-{algorithm}",
            "--from-code",
            encoding,
            "--to-code",
            encoding,
        ],
        capture_output=True,
        input=contents,
    )
    assert uconv_result.returncode == 0
    if xxu_result.stdout != uconv_result.stdout:
        print(
            f"got output mismatch for input {name} (algorithm: {algorithm}, encoding: {encoding})"
        )
        sys.exit(1)


xxu_bin = sys.argv[1]

for input_dir in sys.argv[2:]:
    for file in Path(input_dir).iterdir():
        name = str(file)
        contents = file.read_bytes()
        for algorithm in ALGORITHMS:
            for encoding in ENCODINGS:
                if "canon_order_boundary.txt" in name and encoding == "utf-16le":
                    print(
                        f"SKIPPED {name} (algorithm: {algorithm}, encoding: {encoding})"
                    )
                    print("Reason: appears to be a bug in uconv (ICU 77.1)")
                    continue
                encoded = contents.decode("UTF-8").encode(encoding)
                compare(xxu_bin, algorithm, name, encoded, encoding)
                if len(encoded) < 256:
                    extended = encoded + b"0" * (256 - len(encoded))
                    compare(xxu_bin, algorithm, f"EXTENDED-{name}", extended, encoding)

print("All xxu tests passed!")
