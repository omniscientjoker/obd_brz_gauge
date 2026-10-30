#!/usr/bin/env python3
"""Package build-master/ and build-slave/ without invoking Git or flashing.

Each target has its own manifest and flash_args. The root latest.json remains
the master OTA manifest; its paths point into master/ for existing clients.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parent.parent
BINS = {
    "firmware": "obd_brz_gauge.bin",
    "bootloader": "bootloader/bootloader.bin",
    "partition_table": "partition_table/partition-table.bin",
    "ota_data_initial": "ota_data_initial.bin",
    "bootmedia": "bootmedia.bin",
}
BOARDS = {
    "master": "Waveshare ESP32-S3-Touch-LCD-1.85",
    "slave": "Waveshare ESP32-S3-LCD-1.85",
}


def file_info(path, relative):
    return {"path": relative, "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def read_build(build_dir, target):
    config = (build_dir / "config/sdkconfig.h").read_text()
    other = "slave" if target == "master" else "master"
    if (f"#define CONFIG_GAUGE_FIRMWARE_{target.upper()} 1" not in config or
            f"#define CONFIG_GAUGE_FIRMWARE_{other.upper()} 1" in config):
        raise ValueError(f"{build_dir}: firmware target is not {target}")
    project = json.loads((build_dir / "project_description.json").read_text())
    flash = json.loads((build_dir / "flasher_args.json").read_text())
    ninja = (build_dir / "build.ninja").read_text()

    def define(name, quoted=True):
        pattern = (rf'-D{name}=\\"([^"\\]+)\\"' if quoted
                   else rf'-D{name}=(\d+)\b')
        match = re.search(pattern, ninja)
        if not match:
            raise ValueError(f"{build_dir}: missing build metadata {name}")
        return match.group(1)

    metadata = {
        "project": project["project_name"],
        "version": project["project_version"],
        "target": target,
        "build_tag": define("OBD_GAUGE_BUILD_TAG"),
        "git": define("OBD_GAUGE_GIT_HASH"),
        "branch": define("OBD_GAUGE_GIT_BRANCH"),
        "count": int(define("OBD_GAUGE_GIT_COUNT", quoted=False)),
        "idf": define("IDF_VER"),
        "slot": "ota_0",
        "built": datetime.fromtimestamp(
            (build_dir / BINS["firmware"]).stat().st_mtime,
            timezone.utc).isoformat(),
    }
    for relative in BINS.values():
        if not (build_dir / relative).is_file():
            raise ValueError(f"missing build artifact: {build_dir / relative}")
    if set(flash["flash_files"].values()) != set(BINS.values()):
        raise ValueError(f"{build_dir}: unexpected flash files; update the packager")
    return metadata, flash


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def package_target(build_dir, target, output, metadata, flash):
    destination = output / target
    destination.mkdir()
    paths = dict(BINS)
    paths["firmware"] = f"obd_brz_gauge_{target}.bin"
    files = {}
    for key, source in BINS.items():
        relative = paths[key]
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(build_dir / source, path)
        files[key] = file_info(path, relative)
    if target == "master":
        path = destination / "vehicle_fuel_tanks.json"
        shutil.copy2(ROOT / "config/vehicle_fuel_tanks.json", path)
        files["vehicle_fuel_tanks"] = file_info(path, path.name)
    manifest = {
        "device": {
            "board": BOARDS[target], "variant": "obd_brz_gauge",
            "lcd": "ST77916", "screen": {"w": 360, "h": 360, "bpp": 16},
            "flash_mb": 16, "psram_mb": 8, "ota_slots": 2,
            "bootmedia_slots": 1, "bootmedia_format": 1,
        },
        "firmware": metadata,
        "files": files,
    }
    write_json(destination / "latest.json", manifest)
    args = list(flash["write_flash_args"])
    address_map = []
    for offset, source in sorted(flash["flash_files"].items(), key=lambda item: int(item[0], 0)):
        relative = paths["firmware"] if source == BINS["firmware"] else source
        args.extend([offset, relative])
        address_map.append(f"{offset:12} {relative}")
    (destination / "flash_args").write_text("\n".join(args) + "\n")
    (destination / "flash_address_map.txt").write_text("\n".join(address_map) + "\n")
    return manifest


def generate_release(master_build, slave_build, output):
    # Check both targets before replacing any existing release files.
    builds = {"master": master_build, "slave": slave_build}
    inputs = {target: read_build(path, target) for target, path in builds.items()}
    for key in ("git", "branch", "count", "build_tag", "idf"):
        if inputs["master"][0][key] != inputs["slave"][0][key]:
            raise ValueError(f"master/slave build metadata differs: {key}; rebuild both targets")
    if output.resolve() in {ROOT, ROOT / "firmware", master_build.resolve(), slave_build.resolve()}:
        raise ValueError("output must be a dedicated release directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".release-", dir=output.parent) as temp:
        staged = Path(temp) / "package"
        staged.mkdir()
        for target, path in builds.items():
            manifest = package_target(path, target, staged, *inputs[target])
            if target == "master":
                for info in manifest["files"].values():
                    info["path"] = "master/" + info["path"]
                write_json(staged / "latest.json", manifest)
        old = Path(temp) / "previous"
        if output.exists():
            output.rename(old)
        try:
            staged.rename(output)
        except OSError:
            if old.exists():
                old.rename(output)
            raise
    print(f"Packaged master + slave: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--master-build-dir", type=Path, default=ROOT / "build-master")
    parser.add_argument("--slave-build-dir", type=Path, default=ROOT / "build-slave")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "firmware/release")
    args = parser.parse_args()
    try:
        generate_release(args.master_build_dir, args.slave_build_dir, args.output_dir)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Package failed: {error}\nBuild both targets with tools/release.sh first.\n")


if __name__ == "__main__":
    main()
