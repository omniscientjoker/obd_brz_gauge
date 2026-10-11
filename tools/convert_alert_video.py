#!/usr/bin/env python3
"""Convert an MP4/AVI/etc. into a video alert resource for the SD card.

The output is a pair of files consumed by the device's alert-video player:

    <output>/<name>.TXT   delta-varint manifest
    <output>/<name>.BIN   RGB565 delta stream

Requirements:
    ffmpeg in PATH
    Pillow (``python3 -m pip install Pillow``)

Example:
    python3 tools/convert_alert_video.py alert.mp4 \
        --name tpms_alert --output /Volumes/SDCARD/ALERT

MP4 audio is intentionally ignored. The alert-video player only renders the
video stream; use a separate .WAV resource for an audible alert.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
ENCODER = SCRIPT_DIR / "make_boot_block.py"
MAX_RESOURCE_NAME = 64
MAX_FRAMES = 0xFFFF


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"ERROR: {message}")


def validate_name(raw_name: str) -> str:
    if "/" in raw_name or "\\" in raw_name:
        fail("--name must be a single file stem without directories")
    name = Path(raw_name).name
    for suffix in (".TXT", ".txt", ".BIN", ".bin"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break
    if not name or name in (".", ".."):
        fail("--name must be a single file stem without directories")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        fail("--name may contain only ASCII letters, digits, '_' and '-'")
    if len(name) + 4 > MAX_RESOURCE_NAME:
        fail(f"resource name is too long; <name>.TXT must be <= {MAX_RESOURCE_NAME} bytes")
    return name


def parse_manifest(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def run_encoder(args: argparse.Namespace, temp_dir: Path, input_path: Path) -> None:
    cmd = [
        sys.executable,
        str(ENCODER),
        str(input_path),
        "--canvas",
        str(args.canvas),
        "--grid",
        str(args.grid),
        "--fps",
        str(args.fps),
        "--threshold",
        str(args.threshold),
        "--shift-up",
        str(args.shift_up),
        "--no-audio",
        "--output",
        str(temp_dir),
    ]
    if args.duration > 0:
        cmd.extend(["--duration", str(args.duration)])
    print("Running:", " ".join(cmd))
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        fail("Python interpreter or make_boot_block.py was not found")
    except subprocess.CalledProcessError as exc:
        fail(f"video encoding failed with exit code {exc.returncode}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert a video into an SD-card alert resource (.TXT + .BIN)"
    )
    parser.add_argument("input", type=Path, help="input video file")
    parser.add_argument("--name", required=True, help="resource stem, e.g. tpms_alert")
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=Path("sdcard") / "ALERT",
        help="destination directory (default: ./sdcard/ALERT)",
    )
    parser.add_argument("--canvas", type=int, default=360, help="canvas size (default: 360)")
    parser.add_argument("--grid", type=int, default=180, help="encoding grid (default: 180)")
    parser.add_argument("--fps", type=int, default=15, help="output frame rate (default: 15)")
    parser.add_argument(
        "--threshold",
        type=float,
        default=40.0,
        help="black-pixel luminance threshold (default: 40)",
    )
    parser.add_argument("--shift-up", type=int, default=0, help="shift content up in pixels")
    parser.add_argument("--duration", type=float, default=0, help="trim duration in seconds")
    parser.add_argument(
        "--force",
        action="store_true",
        help="replace existing <name>.TXT and <name>.BIN",
    )
    args = parser.parse_args()

    input_path = args.input.expanduser().resolve()
    if not input_path.is_file():
        fail(f"input video does not exist: {input_path}")
    if not ENCODER.is_file():
        fail(f"encoder not found: {ENCODER}")
    if args.canvas <= 0 or args.canvas > 512:
        fail("--canvas must be between 1 and 512")
    if args.grid <= 0 or args.grid > args.canvas:
        fail("--grid must be between 1 and --canvas")
    if args.fps <= 0 or args.fps > 120:
        fail("--fps must be between 1 and 120")
    if args.duration < 0:
        fail("--duration cannot be negative")

    name = validate_name(args.name)
    output_dir = args.output.expanduser().resolve()
    manifest_out = output_dir / f"{name}.TXT"
    data_out = output_dir / f"{name}.BIN"
    if not args.force and (manifest_out.exists() or data_out.exists()):
        fail(f"output exists; use --force to replace {manifest_out} and/or {data_out}")
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="alert_video_") as temp_name:
        temp_dir = Path(temp_name)
        run_encoder(args, temp_dir, input_path)
        source_manifest = temp_dir / "boot_block.txt"
        source_data = temp_dir / "boot_block.bin"
        if not source_manifest.is_file() or not source_data.is_file():
            fail("encoder did not produce boot_block.txt and boot_block.bin")

        values = parse_manifest(source_manifest)
        try:
            frame_count = int(values["frame_count"])
        except (KeyError, ValueError):
            fail("encoder manifest has no valid frame_count")
        if frame_count <= 0 or frame_count > MAX_FRAMES:
            fail(f"video has {frame_count} frames; device limit is {MAX_FRAMES}")

        values["data_file"] = f"{name}.BIN"
        values["binary_size"] = str(source_data.stat().st_size)
        manifest_text = "".join(f"{key}={value}\n" for key, value in values.items())

        # Write temporary files beside the final output, then replace them so
        # an interrupted conversion never leaves a half-valid resource pair.
        temp_manifest = output_dir / f".{name}.TXT.tmp"
        temp_data = output_dir / f".{name}.BIN.tmp"
        try:
            temp_manifest.write_text(manifest_text, encoding="ascii")
            shutil.copyfile(source_data, temp_data)
            os.replace(temp_manifest, manifest_out)
            os.replace(temp_data, data_out)
        finally:
            temp_manifest.unlink(missing_ok=True)
            temp_data.unlink(missing_ok=True)

    print("\nDevice resource ready:")
    print(f"  manifest : {manifest_out} ({manifest_out.stat().st_size} bytes)")
    print(f"  data     : {data_out} ({data_out.stat().st_size} bytes)")
    print("\nCopy both files into the SD card's ALERT directory.")
    print(f"Select {manifest_out.name} in Settings -> Alert Media -> Video.")


if __name__ == "__main__":
    main()
