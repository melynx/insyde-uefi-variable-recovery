#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Select an NVRAM seed and print or run the Linux recovery-module command."""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from pathlib import Path

from nvram_records import choose_seed, choose_store, module_arguments, parse_image


def parse_offset(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="a live SPI dump")
    parser.add_argument("module", type=Path, help="built and signed .ko file")
    parser.add_argument("--store-offset", type=parse_offset)
    parser.add_argument(
        "--load",
        action="store_true",
        help="run insmod; without this flag the tool only prints the command",
    )
    parser.add_argument(
        "--expose",
        action="store_true",
        help="pass expose=1; first run without this flag and inspect dmesg",
    )
    args = parser.parse_args()

    try:
        stores = parse_image(args.image.read_bytes())
        store = choose_store(stores, args.store_offset)
        seed = choose_seed(store)
        module_path = args.module.resolve(strict=True)
    except (OSError, ValueError) as error:
        print(f"recover_from_nvram.py: {error}", file=sys.stderr)
        return 2

    command = ["insmod", str(module_path), *module_arguments(seed)]
    if args.expose:
        command.append("expose=1")

    print(f"selected_store=0x{store.header.offset:x}")
    print(f"selected_record=0x{seed.offset:x}")
    print(f"seed={seed.name}-{seed.guid}")
    print(f"attributes=0x{seed.attributes:08x}")
    print("command=" + shlex.join(["sudo", *command]))

    if not args.load:
        print("No command was run. Add --load for the dry module test.")
        return 0
    if os.geteuid() != 0:
        print("recover_from_nvram.py: --load requires root", file=sys.stderr)
        return 2

    completed = subprocess.run(command, check=False)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
