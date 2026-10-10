#!/usr/bin/env python3
"""Capture the Ford PCM response to the read-only UDS DID 22 F4 2F."""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - depends on the Windows host
    print("Missing dependency: pyserial")
    print("Install with: py -m pip install pyserial")
    raise SystemExit(2) from exc

BAUDRATE = 38400
PORT_CANDIDATES = ("COM4", "COM7")
PCM_HEADER = "7E0"
OBD_FUNCTIONAL_HEADER = "7DF"
FUEL_REQUESTS = (
    ("standard_pid_availability", OBD_FUNCTIONAL_HEADER, "01 20"),
    ("standard_pid", OBD_FUNCTIONAL_HEADER, "01 2F"),
    ("ford_uds_did", PCM_HEADER, "22 F4 2F"),
)


def local_timestamp() -> str:
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def decode_ascii(data: bytes) -> str:
    return data.decode("ascii", errors="backslashreplace")


def analyze_response(text: str) -> dict[str, Any]:
    """Build a token view without assuming the CAN frame layout."""
    hex_lines: list[dict[str, Any]] = []
    for line in text.replace("\r", "\n").split("\n"):
        tokens = [token.upper() for token in line.strip().split()]
        header = None
        byte_tokens = tokens
        if tokens and len(tokens[0]) in (3, 8) and all(c in "0123456789ABCDEF" for c in tokens[0]):
            header = tokens[0]
            byte_tokens = tokens[1:]
        if byte_tokens and all(len(token) == 2 and all(c in "0123456789ABCDEF" for c in token)
                               for token in byte_tokens):
            hex_lines.append({"can_header": header, "data_bytes": byte_tokens})

    flat = [token for line in hex_lines for token in line["data_bytes"]]
    joined = " ".join(flat)
    if "7F 01" in joined or "7F 22" in joined:
        status = "diagnostic_negative_response"
    elif "41" in flat or "62" in flat:
        status = "possible_positive_response"
    elif "NO DATA" in text.upper():
        status = "no_data"
    elif "?" in text:
        status = "adapter_error"
    else:
        status = "unclassified"
    return {
        "response_status_candidate": status,
        "hex_token_lines": hex_lines,
        "hex_tokens_flat": flat,
        "can_headers_seen": list(dict.fromkeys(
            line["can_header"] for line in hex_lines if line["can_header"]
        )),
    }


def read_response(port: serial.Serial, timeout_s: float) -> bytes:
    """Read all ELM UART data up to its prompt or the timeout."""
    result = bytearray()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        chunk = port.read(max(1, port.in_waiting))
        if chunk:
            result.extend(chunk)
            if b">" in result:
                break
    return bytes(result)


def exchange(port: serial.Serial, command: str, timeout_s: float) -> dict[str, Any]:
    tx = (command + "\r").encode("ascii")
    port.reset_input_buffer()
    started = time.monotonic()
    sent_at = local_timestamp()
    port.write(tx)
    port.flush()
    rx = read_response(port, timeout_s)
    record = {
        "timestamp_local": sent_at,
        "command": command,
        "tx_hex": tx.hex(" ").upper(),
        "rx_hex": rx.hex(" ").upper(),
        "rx_ascii": decode_ascii(rx),
        "elapsed_ms": round((time.monotonic() - started) * 1000, 1),
        "timed_out": b">" not in rx,
    }
    record.update(analyze_response(record["rx_ascii"]))
    return record


def probe_port(device: str, timeout_s: float) -> tuple[bool, dict[str, Any]]:
    """Identify an ELM-compatible serial port using only an ATZ reset."""
    try:
        with serial.Serial(device, BAUDRATE, serial.EIGHTBITS, serial.PARITY_NONE,
                           serial.STOPBITS_ONE, timeout=0.1, write_timeout=2,
                           xonxoff=False, rtscts=False, dsrdtr=False) as port:
            time.sleep(0.25)
            port.reset_input_buffer()
            port.write(b"ATZ\r")
            port.flush()
            response = read_response(port, timeout_s)
        text = decode_ascii(response).upper()
        recognized = b">" in response and ("ELM" in text or "VLINKER" in text or "OK" in text)
        return recognized, {
            "port": device, "command": "ATZ", "tx_hex": "41 54 5A 0D",
            "rx_hex": response.hex(" ").upper(), "rx_ascii": decode_ascii(response),
            "recognized_as_adapter": recognized,
        }
    except (serial.SerialException, OSError) as exc:
        return False, {
            "port": device, "command": "ATZ", "tx_hex": "41 54 5A 0D",
            "rx_hex": "", "rx_ascii": "", "recognized_as_adapter": False,
            "error": f"{type(exc).__name__}: {exc}",
        }


def list_candidate_ports() -> list[dict[str, str]]:
    listed = {item.device.upper(): item for item in list_ports.comports()}
    return [{
        "device": name,
        "description": (item.description or "") if (item := listed.get(name)) else "not listed",
        "manufacturer": (item.manufacturer or "") if item else "",
        "hwid": item.hwid if item else "",
    } for name in PORT_CANDIDATES]


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def fuel_candidate(record: dict, response_prefix: tuple[str, ...]) -> dict:
    """Decode only the exact one-byte positive response shape we are testing."""
    tokens = record.get("hex_tokens_flat", [])
    prefix = list(response_prefix)
    for index in range(len(tokens) - len(prefix)):
        if tokens[index:index + len(prefix)] != prefix:
            continue
        payload = tokens[index + len(prefix):]
        if len(payload) != 1:
            return {
                "status": "positive_response_unexpected_length",
                "response_prefix": " ".join(prefix),
                "payload_hex": payload,
            }
        raw = int(payload[0], 16)
        return {
            "status": "one_byte_positive_response",
            "response_prefix": " ".join(prefix),
            "payload_hex": payload,
            "raw": raw,
            "fuel_percent_candidate": raw * 100.0 / 255.0,
            "formula_candidate": "A * 100 / 255",
        }
    return {"status": "no_matching_positive_response",
            "response_prefix": " ".join(prefix)}


def pid_2f_supported(record: dict) -> dict:
    """Interpret only the SAE 01 20 supported-PID bitmap."""
    tokens = record.get("hex_tokens_flat", [])
    for index in range(len(tokens) - 5):
        if tokens[index:index + 2] != ["41", "20"]:
            continue
        payload = tokens[index + 2:index + 6]
        if len(payload) != 4:
            break
        bitmap = int("".join(payload), 16)
        return {
            "status": "availability_response",
            "bitmap_hex": " ".join(payload),
            "pid_2f_supported": bool(bitmap & (1 << (0x40 - 0x2F))),
        }
    return {"status": "no_matching_41_20_response"}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare standard 01 2F and Ford PCM 22 F4 2F fuel-level responses."
    )
    parser.add_argument("--port", choices=("auto", "COM4", "COM7"), default="auto")
    parser.add_argument("--output", type=Path, default=Path("ford_fuel_capture"))
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--response-timeout", type=float, default=3.0)
    args = parser.parse_args()
    if args.rounds < 1 or args.rounds > 20:
        parser.error("--rounds must be between 1 and 20")

    args.output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    stem = f"ford_fuel_{stamp}"
    transcript_path = args.output / f"{stem}.txt"
    json_path = args.output / f"{stem}.json"
    raw_path = args.output / f"{stem}_uart.bin"
    transcript: list[str] = []
    records: list[dict] = []

    ports = list_candidate_ports()
    candidates = PORT_CANDIDATES if args.port == "auto" else (args.port,)
    selected_port = None
    probes = []
    for device in candidates:
        ok, probe = probe_port(device, args.response_timeout)
        probe["timestamp_local"] = local_timestamp()
        probes.append(probe)
        if ok:
            selected_port = device
            break

    if not selected_port:
        write_json(json_path, {
            "schema": "ford_fuel_capture_v1",
            "selected_port": None,
            "ports": ports,
            "probe_results": probes,
            "exchanges": [],
        })
        print(f"No ELM-like adapter found. Saved {json_path.resolve()}")
        return 3

    setup_commands = ("ATZ", "ATE0", "ATL0", "ATS1", "ATH1", "ATSP6", "ATCAF1")
    try:
        with serial.Serial(selected_port, BAUDRATE, serial.EIGHTBITS,
                           serial.PARITY_NONE, serial.STOPBITS_ONE,
                           timeout=0.1, write_timeout=2,
                           xonxoff=False, rtscts=False, dsrdtr=False) as port:
            time.sleep(0.3)
            for command in setup_commands:
                record = exchange(port, command, args.response_timeout)
                record["kind"] = "adapter_setup"
                records.append(record)
            active_header = None
            for round_index in range(1, args.rounds + 1):
                for request_kind, request_header, command in FUEL_REQUESTS:
                    if request_header != active_header:
                        header_record = exchange(port, f"ATSH{request_header}", args.response_timeout)
                        header_record["kind"] = "adapter_header"
                        records.append(header_record)
                        active_header = request_header
                    is_standard_pid = request_kind == "standard_pid"
                    record = exchange(port, command, args.response_timeout)
                    record.update({
                        "kind": request_kind,
                        "round": round_index,
                        "request_header": request_header,
                        "service": "01" if request_kind.startswith("standard_pid") else "22",
                        "pid": "20" if request_kind == "standard_pid_availability" else ("2F" if is_standard_pid else None),
                        "did": "F42F" if request_kind == "ford_uds_did" else None,
                    })
                    if request_kind == "standard_pid_availability":
                        record["pid_availability"] = pid_2f_supported(record)
                    else:
                        record["fuel_decode"] = fuel_candidate(
                            record, ("41", "2F") if is_standard_pid else ("62", "F4", "2F")
                        )
                    records.append(record)
                    decoded = record.get("pid_availability", record.get("fuel_decode"))
                    transcript.append(
                        f"[{record['timestamp_local']}] ROUND {round_index} "
                        f"{request_kind} header={request_header} TX {command!r}\n"
                        f"RX HEX: {record['rx_hex']}\nRX ASCII: {record['rx_ascii']!r}\n"
                        f"decode: {json.dumps(decoded, ensure_ascii=False)}"
                    )
                    print(transcript[-1])
                    time.sleep(0.2)
            exchange(port, f"ATSH{OBD_FUNCTIONAL_HEADER}", args.response_timeout)
    except (serial.SerialException, OSError) as exc:
        transcript.append(f"ERROR: {type(exc).__name__}: {exc}")

    with raw_path.open("wb") as raw_file:
        for index, record in enumerate(records, start=1):
            raw_file.write(f"\n--- RECORD {index} {record['command']} ---\n".encode("ascii"))
            raw_file.write(bytes.fromhex(record["tx_hex"]))
            raw_file.write(b"\n< RX >\n")
            raw_file.write(bytes.fromhex(record["rx_hex"]))
            raw_file.write(b"\n")

    transcript_path.write_text("\n\n".join(transcript) + "\n", encoding="utf-8")
    write_json(json_path, {
        "schema": "ford_fuel_capture_v1",
        "started_local": local_timestamp(),
        "started_utc": utc_timestamp(),
        "selected_port": selected_port,
        "ports": ports,
        "probe_results": probes,
        "vehicle_context": {
            "reported_vehicle": "Ford Mondeo 2013/2014 2.0T (model year to confirm)",
            "ecu_candidate": "PCM",
            "ecu_header": PCM_HEADER,
            "requests": [
                {"service": "01", "pid": "2F", "expected_response": "41 2F AA"},
                {"service": "22", "did": "F42F", "expected_response": "62 F4 2F AA"},
            ],
        },
        "exchanges": records,
    })
    print(f"Saved transcript: {transcript_path.resolve()}")
    print(f"Saved structured log: {json_path.resolve()}")
    print(f"Saved UART byte log: {raw_path.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
