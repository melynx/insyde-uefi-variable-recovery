#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Copy a live SPI NVRAM device without opening the source for writing."""

from __future__ import annotations

import argparse
import hashlib
import os
import stat
import sys
from pathlib import Path


CHUNK_SIZE = 1024 * 1024


def validate_source(path: Path, allow_writable_device: bool) -> None:
    mode = path.stat().st_mode
    if stat.S_ISREG(mode):
        return
    if stat.S_ISCHR(mode) and path.name.endswith("ro"):
        return
    if allow_writable_device and (stat.S_ISCHR(mode) or stat.S_ISBLK(mode)):
        return
    raise ValueError(
        "source is not a regular file or a read-only character device whose "
        "name ends in 'ro'; use --allow-writable-device only after checking it"
    )


def copy_source(source: Path, destination: Path, force: bool) -> tuple[int, str]:
    if source.resolve() == destination.resolve():
        raise ValueError("source and destination refer to the same path")

    output_mode = "wb" if force else "xb"
    digest = hashlib.sha256()
    total = 0
    created = False
    try:
        with source.open("rb", buffering=0) as input_file:
            with destination.open(output_mode) as output_file:
                created = True
                while True:
                    block = input_file.read(CHUNK_SIZE)
                    if not block:
                        break
                    output_file.write(block)
                    digest.update(block)
                    total += len(block)
                output_file.flush()
                os.fsync(output_file.fileno())
    except Exception:
        if created:
            try:
                destination.unlink()
            except OSError:
                pass
        raise
    return total, digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="for example /dev/mtd0ro")
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "--allow-writable-device",
        action="store_true",
        help="allow a character or block device without a read-only name",
    )
    parser.add_argument(
        "--force", action="store_true", help="replace an existing destination"
    )
    args = parser.parse_args()

    try:
        validate_source(args.source, args.allow_writable_device)
        size, digest = copy_source(args.source, args.destination, args.force)
    except (OSError, ValueError) as error:
        print(f"dump_nvram.py: {error}", file=sys.stderr)
        return 2

    print(f"source={args.source}")
    print(f"destination={args.destination}")
    print(f"bytes={size}")
    print(f"sha256={digest}")
    print("Treat the output as private machine data.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
