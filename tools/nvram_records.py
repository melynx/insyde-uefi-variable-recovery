#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""List EDK II-style UEFI variable records without printing their data."""

from __future__ import annotations

import argparse
import json
import shlex
import struct
import sys
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path


STORE_HEADER_SIZE = 28
START_ID = 0x55AA
VAR_ADDED = 0x3F
EFI_VARIABLE_RUNTIME_ACCESS = 0x04

STANDARD_STORE_GUID = uuid.UUID("ddcf3616-3275-4164-98b6-fe85707ffe7d")
AUTHENTICATED_STORE_GUID = uuid.UUID("aaf32c78-947b-439a-a180-2e144ec37792")
STORE_SIGNATURES = {
    STANDARD_STORE_GUID.bytes_le: "standard",
    AUTHENTICATED_STORE_GUID.bytes_le: "authenticated",
}


@dataclass(frozen=True)
class RecordLayout:
    name: str
    header_size: int
    name_size_offset: int
    data_size_offset: int
    guid_offset: int


STANDARD_LAYOUT = RecordLayout("standard", 32, 8, 12, 16)
AUTHENTICATED_LAYOUT = RecordLayout("authenticated", 60, 36, 40, 44)
LAYOUTS = (STANDARD_LAYOUT, AUTHENTICATED_LAYOUT)


@dataclass(frozen=True)
class StoreHeader:
    offset: int
    size: int
    format: int
    state: int
    signature: str
    declared_layout: str


@dataclass(frozen=True)
class VariableRecord:
    index: int
    offset: int
    state: int
    attributes: int
    name_size: int
    data_size: int
    guid: str
    name: str

    @property
    def live(self) -> bool:
        return self.state == VAR_ADDED

    @property
    def runtime(self) -> bool:
        return bool(self.attributes & EFI_VARIABLE_RUNTIME_ACCESS)

    @property
    def ascii_name(self) -> bool:
        return bool(self.name) and all(0x20 <= ord(char) <= 0x7E for char in self.name)


@dataclass(frozen=True)
class ParsedStore:
    header: StoreHeader
    record_layout: str
    record_alignment: int
    records: tuple[VariableRecord, ...]
    end_offset: int
    clean_end: bool

    @property
    def live_count(self) -> int:
        return sum(record.live for record in self.records)


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def find_store_headers(image: bytes) -> list[StoreHeader]:
    """Find formatted, healthy-looking EDK II variable-store headers."""

    headers: list[StoreHeader] = []
    for signature_bytes, declared_layout in STORE_SIGNATURES.items():
        start = 0
        while True:
            offset = image.find(signature_bytes, start)
            if offset < 0:
                break
            start = offset + 1
            if offset + STORE_HEADER_SIZE > len(image):
                continue

            size = struct.unpack_from("<I", image, offset + 16)[0]
            format_byte = image[offset + 20]
            state = image[offset + 21]
            if size < STORE_HEADER_SIZE or offset + size > len(image):
                continue
            if format_byte != 0x5A or state != 0xFE:
                continue

            headers.append(
                StoreHeader(
                    offset=offset,
                    size=size,
                    format=format_byte,
                    state=state,
                    signature=str(uuid.UUID(bytes_le=signature_bytes)),
                    declared_layout=declared_layout,
                )
            )
    return sorted(headers, key=lambda header: header.offset)


def parse_record(
    image: bytes,
    offset: int,
    limit: int,
    layout: RecordLayout,
    index: int,
) -> tuple[VariableRecord, int] | None:
    if offset + layout.header_size > limit:
        return None
    if struct.unpack_from("<H", image, offset)[0] != START_ID:
        return None

    state = image[offset + 2]
    attributes = struct.unpack_from("<I", image, offset + 4)[0]
    name_size = struct.unpack_from("<I", image, offset + layout.name_size_offset)[0]
    data_size = struct.unpack_from("<I", image, offset + layout.data_size_offset)[0]
    guid_start = offset + layout.guid_offset
    guid_end = guid_start + 16

    if name_size < 2 or name_size % 2:
        return None
    if guid_end > offset + layout.header_size:
        return None

    name_start = offset + layout.header_size
    name_end = name_start + name_size
    data_end = name_end + data_size
    if name_end < name_start or data_end < name_end or data_end > limit:
        return None

    name_bytes = image[name_start:name_end]
    if not name_bytes.endswith(b"\x00\x00"):
        return None
    try:
        name = name_bytes[:-2].decode("utf-16-le")
    except UnicodeDecodeError:
        return None
    if not name or "\x00" in name:
        return None

    record = VariableRecord(
        index=index,
        offset=offset,
        state=state,
        attributes=attributes,
        name_size=name_size,
        data_size=data_size,
        guid=str(uuid.UUID(bytes_le=image[guid_start:guid_end])),
        name=name,
    )
    return record, data_end


def parse_chain(
    image: bytes,
    header: StoreHeader,
    layout: RecordLayout,
    alignment: int,
) -> ParsedStore:
    limit = header.offset + header.size
    offset = align_up(header.offset + STORE_HEADER_SIZE, 4)
    records: list[VariableRecord] = []

    while offset + 2 <= limit:
        parsed = parse_record(image, offset, limit, layout, len(records) + 1)
        if parsed is None:
            break
        record, data_end = parsed
        records.append(record)
        offset = align_up(data_end, alignment)

    tail = image[offset : min(offset + 64, limit)]
    clean_end = not tail or all(byte in (0x00, 0xFF) for byte in tail)
    return ParsedStore(
        header=header,
        record_layout=layout.name,
        record_alignment=alignment,
        records=tuple(records),
        end_offset=offset,
        clean_end=clean_end,
    )


def parse_store(image: bytes, header: StoreHeader) -> ParsedStore:
    candidates = [
        parse_chain(image, header, layout, alignment)
        for layout in LAYOUTS
        for alignment in (1, 4)
    ]

    def score(store: ParsedStore) -> tuple[int, int, int, int]:
        declared_match = int(store.record_layout == header.declared_layout)
        return (
            int(store.clean_end),
            len(store.records),
            store.end_offset - header.offset,
            declared_match,
        )

    return max(candidates, key=score)


def parse_image(image: bytes) -> list[ParsedStore]:
    return [parse_store(image, header) for header in find_store_headers(image)]


def choose_store(
    stores: list[ParsedStore], requested_offset: int | None = None
) -> ParsedStore:
    if requested_offset is not None:
        for store in stores:
            if store.header.offset == requested_offset:
                if not store.clean_end:
                    raise ValueError(
                        f"store at 0x{requested_offset:x} does not have a "
                        "clean record-chain end"
                    )
                return store
        raise ValueError(f"no parsed store starts at 0x{requested_offset:x}")
    if not stores:
        raise ValueError("no formatted, healthy EDK II variable store was found")

    clean_stores = [store for store in stores if store.clean_end]
    if not clean_stores:
        raise ValueError("no parsed store has a clean record-chain end")

    # A live SPI image can contain factory templates and vendor fallback stores.
    # The store with the largest live set is the safest automatic candidate.
    return max(
        clean_stores,
        key=lambda store: (
            store.live_count,
            len(store.records),
            store.header.size,
        ),
    )


def choose_seed(store: ParsedStore) -> VariableRecord:
    for record in store.records:
        if record.live and record.runtime and record.ascii_name:
            return record
    raise ValueError(
        f"store at 0x{store.header.offset:x} has no live runtime record "
        "with an ASCII name"
    )


def module_arguments(seed: VariableRecord) -> list[str]:
    return [f"seed_name={seed.name}", f"seed_guid={seed.guid}"]


def state_name(state: int) -> str:
    return {
        0x3F: "ADDED",
        0x7F: "HEADER_VALID_ONLY",
        0x3E: "ADDED_AND_IN_DELETED_TRANSITION",
        0x3D: "ADDED_AND_DELETED",
        0x3C: "ADDED_AND_IN_DELETED_TRANSITION_AND_DELETED",
        0xFD: "DELETED",
        0xFE: "IN_DELETED_TRANSITION",
    }.get(state, f"UNKNOWN_0x{state:02x}")


def print_text(stores: list[ParsedStore], live_only: bool) -> None:
    if not stores:
        print("No formatted, healthy EDK II variable store was found.")
        return

    for store in stores:
        header = store.header
        print(
            f"STORE @0x{header.offset:x} size=0x{header.size:x} "
            f"signature={header.signature} declared={header.declared_layout} "
            f"parsed={store.record_layout} alignment={store.record_alignment} "
            f"records={len(store.records)} live={store.live_count} "
            f"clean_end={str(store.clean_end).lower()}"
        )
        for record in store.records:
            if live_only and not record.live:
                continue
            flags = []
            if record.live:
                flags.append("LIVE")
            if record.runtime:
                flags.append("RT")
            flag_text = ",".join(flags) or "-"
            print(
                f"  {record.index:4d} @0x{record.offset:x} "
                f"state=0x{record.state:02x}({state_name(record.state)}) "
                f"attrs=0x{record.attributes:08x} [{flag_text}] "
                f"data={record.data_size} {record.name}-{record.guid}"
            )


def json_value(stores: list[ParsedStore]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for store in stores:
        item: dict[str, object] = {
            "header": asdict(store.header),
            "record_layout": store.record_layout,
            "record_alignment": store.record_alignment,
            "end_offset": store.end_offset,
            "clean_end": store.clean_end,
            "live_count": store.live_count,
            "records": [],
        }
        records = item["records"]
        assert isinstance(records, list)
        for record in store.records:
            record_value = asdict(record)
            record_value["live"] = record.live
            record_value["runtime"] = record.runtime
            records.append(record_value)
        result.append(item)
    return result


def parse_offset(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="live SPI dump or test image")
    parser.add_argument(
        "--store-offset",
        type=parse_offset,
        help="select one store by byte offset, written as decimal or 0x...",
    )
    parser.add_argument("--live-only", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--seed-only",
        action="store_true",
        help="print the selected candidate seed and module parameters",
    )
    args = parser.parse_args()

    try:
        image = args.image.read_bytes()
        stores = parse_image(image)
        if args.store_offset is not None:
            stores = [choose_store(stores, args.store_offset)]

        if args.seed_only:
            store = choose_store(stores, args.store_offset)
            seed = choose_seed(store)
            value = {
                "store_offset": store.header.offset,
                "record_offset": seed.offset,
                "name": seed.name,
                "guid": seed.guid,
                "attributes": seed.attributes,
                "module_arguments": module_arguments(seed),
            }
            if args.json:
                print(json.dumps(value, indent=2, sort_keys=True))
            else:
                print(f"store_offset=0x{store.header.offset:x}")
                print(f"record_offset=0x{seed.offset:x}")
                print(f"seed_name={seed.name}")
                print(f"seed_guid={seed.guid}")
                print(f"seed_attributes=0x{seed.attributes:08x}")
                print("module_args=" + shlex.join(module_arguments(seed)))
            return 0

        if args.json:
            print(json.dumps(json_value(stores), indent=2, sort_keys=True))
        else:
            print_text(stores, args.live_only)
        return 0 if stores else 1
    except (OSError, ValueError) as error:
        print(f"nvram_records.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
