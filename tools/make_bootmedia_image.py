#!/usr/bin/env python3
"""Build the raw bootmedia image consumed by boot_media_mount.c.

The partition is deliberately not a SPIFFS filesystem.  It contains a
fixed 4 KiB manifest slot followed by the boot animation byte stream.
"""

import argparse
from pathlib import Path


MANIFEST_SLOT_SIZE = 0x1000


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    manifest = args.manifest.read_bytes()
    data = args.data.read_bytes()
    if len(manifest) >= MANIFEST_SLOT_SIZE:
        raise SystemExit("boot manifest must be smaller than 4096 bytes")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as out:
        out.write(manifest)
        out.write(b"\0" * (MANIFEST_SLOT_SIZE - len(manifest)))
        out.write(data)

    print(f"Raw bootmedia image: {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
