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
            f"\ngot output mismatch for input {name} (algorithm: {algorithm}, encoding: {encoding})"
        )
        sys.exit(1)


def progress_bar(iteration, total, length=40):
    if not sys.stderr.isatty():
        return

    percent = iteration / total
    filled = int(length * percent)
    bar = "#" * filled + "-" * (length - filled)
    sys.stderr.write(f"\r[{bar}] {percent:.1%}")
    sys.stderr.flush()


xxu_bin = sys.argv[1]

total_jobs = 0
for input_dir in sys.argv[2:]:
    path = Path(input_dir)
    n_files = sum(1 for _ in path.iterdir())
    total_jobs += n_files * len(ALGORITHMS) * len(ENCODINGS)

PROGRESS_BAR_LENGTH = 40
job = 1
for input_dir in sys.argv[2:]:
    input_dir_path = Path(input_dir)
    for file in Path(input_dir).iterdir():
        name = str(file)
        contents = file.read_bytes()
        for algorithm in ALGORITHMS:
            for encoding in ENCODINGS:
                if "canon_order_boundary.txt" in name and encoding == "utf-16le":
                    progress_bar(job, total_jobs, PROGRESS_BAR_LENGTH)
                    # print(
                    #     f"SKIPPED {name} (algorithm: {algorithm}, encoding: {encoding})"
                    # )
                    # print("Reason: appears to be a bug in uconv (ICU 77.1)")
                    job += 1
                    continue
                encoded = contents.decode("UTF-8").encode(encoding)
                compare(xxu_bin, algorithm, name, encoded, encoding)
                if len(encoded) < 256:
                    extended = encoded + b"0" * (256 - len(encoded))
                    compare(xxu_bin, algorithm, f"EXTENDED-{name}", extended, encoding)
                progress_bar(job, total_jobs, PROGRESS_BAR_LENGTH)
                job += 1

if sys.stderr.isatty():
    print("\rAll xxu tests passed!" + " " * PROGRESS_BAR_LENGTH, file=sys.stderr)
else:
    print("All xxu tests passed!", file=sys.stderr)
