#!/usr/bin/env python3
"""
Encode a video into boot_block.bin + boot_block.txt for the ESP32 boot_block_player.
Format: delta_varint_rgb565_black_v1 (same as hokori_vehicle_gauge).

Usage:
    python3 tools/make_boot_block.py <input_video> [--canvas 360] [--grid 240] [--fps 15] [--output bootmedia/slot_a]

Requirements:
    pip install Pillow
    ffmpeg in PATH
"""

import argparse
import struct
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow not installed. Run: pip install Pillow")
    sys.exit(1)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    r5 = (r * 31 + 127) // 255
    g6 = (g * 63 + 127) // 255
    b5 = (b * 31 + 127) // 255
    return ((r5 & 0x1F) << 11) | ((g6 & 0x3F) << 5) | (b5 & 0x1F)


def append_varint(out: bytearray, value: int) -> None:
    while True:
        chunk = value & 0x7F
        value >>= 7
        if value > 0:
            chunk |= 0x80
        out.append(chunk)
        if value == 0:
            break


def compute_grid_edges(canvas_size: int, grid_size: int) -> list:
    return [int(i * canvas_size / grid_size) for i in range(grid_size + 1)]


def frame_to_cells(img: Image.Image, grid_size: int, x_edges: list, y_edges: list,
                   threshold: float) -> list:
    """Return list of (cell_index, RGB565) values.

    The stream is RGB565, so quantize once here instead of producing RGB888
    values and converting them again while serializing the frame.
    """
    cells = []
    pixels = img.load()
    w, h = img.size
    threshold_i = max(0, min(255, int(round(threshold))))

    # The normal 360x360/full-resolution case can use a compact byte view and
    # avoid tuple creation and bounds checks for every pixel.
    direct_pixels = (w == h == grid_size and
                     all(x_edges[i + 1] - x_edges[i] == 1 for i in range(grid_size)) and
                     all(y_edges[i + 1] - y_edges[i] == 1 for i in range(grid_size)))
    if direct_pixels:
        raw = img.tobytes()
        for idx in range(grid_size * grid_size):
            off = idx * 3
            r, g, b = raw[off], raw[off + 1], raw[off + 2]
            lum = (299 * r + 587 * g + 114 * b + 500) // 1000
            packed = rgb888_to_rgb565(r, g, b) if lum >= threshold_i else 0
            cells.append((idx, packed))
        return cells

    for gy in range(grid_size):
        y0 = y_edges[gy]
        y1 = y_edges[gy + 1]
        if y1 <= y0:
            continue
        for gx in range(grid_size):
            x0 = x_edges[gx]
            x1 = x_edges[gx + 1]
            if x1 <= x0:
                continue

            # 采样格子内所有像素, 用最大亮度判断是否保留 (捕捉淡入细线)
            max_lum = 0
            sum_r = 0
            sum_g = 0
            sum_b = 0
            bright_count = 0
            count = 0

            for y in range(y0, y1):
                for x in range(x0, x1):
                    if x >= w or y >= h:
                        continue
                    r, g, b = pixels[x, y][:3]
                    lum = (299 * r + 587 * g + 114 * b + 500) // 1000
                    if lum > max_lum:
                        max_lum = lum
                    count += 1
                    if lum >= threshold_i:
                        sum_r += r
                        sum_g += g
                        sum_b += b
                        bright_count += 1

            if count == 0 or max_lum < threshold_i:
                packed = 0
            elif bright_count > 0:
                r = sum_r // bright_count
                g = sum_g // bright_count
                b = sum_b // bright_count
                packed = rgb888_to_rgb565(r, g, b)
            else:
                # 最大亮度够但没像素超 threshold: 用所有像素平均色 (极淡的情况)
                packed = 0

            idx = gy * grid_size + gx
            cells.append((idx, packed))

    return cells


def encode_frame_delta(prev_cells: list, curr_cells: list, count_u32: bool = False,
                       return_count: bool = False):
    """Encode changed cells as delta_varint_rgb565_black_v1/v2.

    count_u32=True (v2): 帧变化数用 4 字节 u32, 支持 grid² > 65535 的全分辨率编码。
    """
    changes = []
    for idx, packed in curr_cells:
        if prev_cells[idx] != packed:
            prev_cells[idx] = packed
            changes.append((idx, packed))

    buf = bytearray()
    change_count = len(changes)
    buf += struct.pack('<I' if count_u32 else '<H', change_count)

    prev_idx = -1
    for idx, packed in changes:
        delta = idx if prev_idx < 0 else idx - prev_idx
        is_black = 1 if packed == 0 else 0
        delta_and_black = (delta << 1) | is_black
        append_varint(buf, delta_and_black)

        if not is_black:
            buf.extend((packed & 0xFF, (packed >> 8) & 0xFF))

        prev_idx = idx

    encoded = bytes(buf)
    return (encoded, change_count) if return_count else encoded


def iter_video_frames(input_path: Path, canvas: int, fps: int, shift_up: int,
                      duration: float):
    """Stream scaled frames from ffmpeg without temporary PNG files."""
    vf = f"fps={fps},scale={canvas}:{canvas}:force_original_aspect_ratio=decrease," \
         f"pad={canvas}:{canvas}:(ow-iw)/2:(oh-ih)/2-{shift_up}:black"
    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(input_path),
    ]
    if duration > 0:
        cmd += ["-t", str(duration)]
    cmd += ["-vf", vf, "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    frame_size = canvas * canvas * 3
    try:
        while True:
            raw_buf = bytearray()
            while len(raw_buf) < frame_size:
                chunk = proc.stdout.read(frame_size - len(raw_buf))
                if not chunk:
                    break
                raw_buf.extend(chunk)
            raw = bytes(raw_buf)
            if not raw:
                break
            if len(raw) != frame_size:
                error = proc.stderr.read().decode("utf-8", errors="replace")
                proc.wait()
                raise RuntimeError(f"ffmpeg returned a partial frame: {error[-500:]}")
            yield Image.frombytes("RGB", (canvas, canvas), raw)
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.stdout.close()
        proc.stderr.close()
    if proc.wait() != 0:
        raise RuntimeError("ffmpeg failed while streaming frames")


def main():
    parser = argparse.ArgumentParser(description="Encode video to boot_block format")
    parser.add_argument("input", help="Input video file (mp4/avi/etc)")
    parser.add_argument("--canvas", type=int, default=360, help="Canvas size in pixels (default: 360)")
    parser.add_argument("--grid", type=int, default=240, help="Grid resolution (default: 240)")
    parser.add_argument("--fps", type=int, default=15, help="Target fps (default: 15)")
    parser.add_argument("--threshold", type=float, default=40.0,
                        help="Luminance threshold for black detection (default: 40)")
    parser.add_argument("--shift-up", type=int, default=0, help="Shift content up by N pixels")
    parser.add_argument("--output", "-o", default="", help="Output directory (default: bootmedia/slot_a)")
    parser.add_argument("--no-audio", action="store_true", help="Skip audio extraction")
    parser.add_argument("--duration", type=float, default=0,
                        help="Force trim video to this duration in seconds (0=auto)")
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    if not input_path.exists():
        print(f"ERROR: Input not found: {input_path}")
        sys.exit(1)

    # Determine output dir
    if args.output:
        output_dir = Path(args.output).resolve()
    else:
        output_dir = Path(__file__).parent.parent / "bootmedia" / "slot_a"
    output_dir.mkdir(parents=True, exist_ok=True)

    canvas = args.canvas
    grid = args.grid
    fps = args.fps

    if not 1 <= canvas <= 512:
        print("ERROR: --canvas must be between 1 and 512")
        sys.exit(1)
    if not 1 <= grid <= canvas:
        print("ERROR: --grid must be between 1 and --canvas")
        sys.exit(1)
    if not 1 <= fps <= 120:
        print("ERROR: --fps must be between 1 and 120")
        sys.exit(1)
    if args.threshold < 0 or args.threshold > 255:
        print("ERROR: --threshold must be between 0 and 255")
        sys.exit(1)

    # grid² > 65535 时单帧变化数会超 u16 上限, 自动切 v2 格式 (4字节计数, 播放器按 manifest 识别)
    use_v2 = grid * grid > 0xFFFF
    stream_format = "delta_varint_rgb565_black_v2" if use_v2 else "delta_varint_rgb565_black_v1"

    print(f"Encoding boot block media")
    print(f"  input    : {input_path}")
    print(f"  output   : {output_dir}")
    print(f"  canvas   : {canvas}x{canvas}")
    print(f"  grid     : {grid}x{grid}")
    print(f"  fps      : {fps}")
    print(f"  threshold: {args.threshold}")

    # Extract audio separately; video frames are streamed below and never
    # materialized as temporary PNG files.
    if not args.no_audio:
        pcm_path = output_dir / "boot.pcm"
        cmd_audio = [
            "ffmpeg", "-y", "-i", str(input_path),
            "-vn", "-ac", "1", "-ar", "16000", "-f", "s16le", str(pcm_path)
        ]
        subprocess.run(cmd_audio, capture_output=True)
        if pcm_path.exists():
            print(f"  Audio: {pcm_path} ({pcm_path.stat().st_size} bytes)")

    # Encode frames
    x_edges = compute_grid_edges(canvas, grid)
    y_edges = compute_grid_edges(canvas, grid)
    prev_cells = [-1] * (grid * grid)
    data_path = output_dir / "boot_block.bin"
    total_changes = 0
    max_changes = 0
    frame_count = 0

    with open(data_path, "wb") as f:
        try:
            frame_iter = iter_video_frames(input_path, canvas, fps, args.shift_up, args.duration)
            for frame_count, img in enumerate(frame_iter, start=1):
                curr_cells = frame_to_cells(img, grid, x_edges, y_edges, args.threshold)
                frame_data, change_count = encode_frame_delta(
                    prev_cells, curr_cells, count_u32=use_v2, return_count=True)
                f.write(frame_data)

                total_changes += change_count
                max_changes = max(max_changes, change_count)

                if frame_count % 20 == 0:
                    print(f"  Frame {frame_count}: {change_count} changes")
        except RuntimeError as exc:
            print(f"ERROR: {exc}")
            sys.exit(1)

    if frame_count == 0:
        print("ERROR: No frames extracted")
        sys.exit(1)
    print(f"  Extracted {frame_count} frames")

    # Write manifest
    duration_ms = int(round(frame_count * 1000.0 / fps))
    manifest_path = output_dir / "boot_block.txt"
    manifest_lines = [
        f"canvas_width={canvas}",
        f"canvas_height={canvas}",
        f"grid_width={grid}",
        f"grid_height={grid}",
        f"fps={fps}",
        f"frame_count={frame_count}",
        f"duration_ms={duration_ms}",
        f"stream_format={stream_format}",
        "data_file=boot_block.bin",
    ]
    manifest_path.write_text("\n".join(manifest_lines) + "\n")

    avg_changes = total_changes / frame_count
    print(f"\nDone!")
    print(f"  {manifest_path} ({manifest_path.stat().st_size} bytes)")
    print(f"  {data_path} ({data_path.stat().st_size} bytes)")
    print(f"  frames         : {frame_count}")
    print(f"  duration       : {duration_ms}ms ({duration_ms/1000:.1f}s)")
    print(f"  avg changes    : {avg_changes:.1f}")
    print(f"  peak changes   : {max_changes}")
    print(f"\nNext steps:")
    print(f"  1. Copy {output_dir}/ to your bootmedia partition")
    print(f"  2. Flash with: python -m espflash write_partition bootmedia {output_dir}/")
    print(f"     Or use parttool.py / esptool.py to write the bootmedia partition")


if __name__ == "__main__":
    main()
