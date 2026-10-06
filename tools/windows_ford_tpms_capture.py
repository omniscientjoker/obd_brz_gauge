#!/usr/bin/env python3
"""Capture candidate Ford BCM TPMS Mode 22 exchanges over an ELM327 serial port.

The script records the exact UART bytes sent to and received from the ELM327
adapter. It does not capture raw CAN electrical/bus traffic or alter vehicle
configuration.
"""

from __future__ import annotations

import argparse
import json
import platform
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
DID_REQUESTS = (
    ("FL", "22 2813"),
    ("FR", "22 2814"),
    ("RL", "22 2816"),
    ("RR", "22 2815"),
)


def local_timestamp() -> str:
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def decode_ascii(data: bytes) -> str:
    return data.decode("ascii", errors="backslashreplace")


def analyze_response(text: str) -> dict[str, Any]:
    """Add a token view without assuming the CAN payload layout."""
    hex_lines: list[dict[str, Any]] = []
    for line in text.replace("\r", "\n").split("\n"):
        tokens = [token.upper() for token in line.strip().split()]
        header = None
        byte_tokens = tokens
        if tokens and len(tokens[0]) in (3, 8):
            candidate = tokens[0]
            if all(c in "0123456789ABCDEF" for c in candidate):
                header = candidate
                byte_tokens = tokens[1:]
        if byte_tokens and all(len(token) == 2 and all(c in "0123456789ABCDEF"
                                                       for c in token)
                               for token in byte_tokens):
            hex_lines.append({"can_header": header, "data_bytes": byte_tokens})

    flat = [token for line in hex_lines for token in line["data_bytes"]]
    joined = " ".join(flat)
    if "7F 22" in joined:
        status = "uds_negative_response"
    elif "62" in flat:
        status = "possible_uds_positive_response"
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
    """Read until ELM prompt or timeout, preserving every received byte."""
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
    elapsed_ms = round((time.monotonic() - started) * 1000, 1)
    record = {
        "timestamp_local": sent_at,
        "command": command,
        "tx_hex": tx.hex(" ").upper(),
        "rx_hex": rx.hex(" ").upper(),
        "rx_ascii": decode_ascii(rx),
        "elapsed_ms": elapsed_ms,
        "timed_out": b">" not in rx,
    }
    record.update(analyze_response(record["rx_ascii"]))
    return record


def probe_port(device: str, open_timeout_s: float) -> tuple[bool, dict[str, Any]]:
    """Identify an ELM-like adapter using only the harmless ATZ reset command."""
    try:
        with serial.Serial(
            port=device,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=2,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as port:
            time.sleep(0.25)
            port.reset_input_buffer()
            port.write(b"ATZ\r")
            port.flush()
            response = read_response(port, open_timeout_s)
        text = decode_ascii(response).upper()
        recognized = b">" in response and (
            "ELM" in text or "VLINKER" in text or "OK" in text
        )
        return recognized, {
            "port": device,
            "command": "ATZ",
            "tx_hex": "41 54 5A 0D",
            "rx_hex": response.hex(" ").upper(),
            "rx_ascii": decode_ascii(response),
            "recognized_as_adapter": recognized,
        }
    except (serial.SerialException, OSError) as exc:
        return False, {
            "port": device,
            "command": "ATZ",
            "tx_hex": "41 54 5A 0D",
            "rx_hex": "",
            "rx_ascii": "",
            "recognized_as_adapter": False,
            "error": f"{type(exc).__name__}: {exc}",
        }


def list_candidate_ports() -> list[dict[str, str]]:
    by_name: dict[str, dict[str, str]] = {}
    for item in list_ports.comports():
        if item.device.upper() in PORT_CANDIDATES:
            by_name[item.device.upper()] = {
                "device": item.device,
                "description": item.description or "",
                "manufacturer": item.manufacturer or "",
                "hwid": item.hwid or "",
            }
    return [by_name.get(name, {"device": name, "description": "not listed"})
            for name in PORT_CANDIDATES]


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Auto-detect COM4/COM7 and record Ford TPMS Mode 22 exchanges."
    )
    parser.add_argument("--port", choices=("auto", "COM4", "COM7"), default="auto",
                        help="port to use; auto probes COM4 and COM7 (default)")
    parser.add_argument("--output", type=Path, default=Path("ford_tpms_capture"),
                        help="output directory (default: ./ford_tpms_capture)")
    parser.add_argument("--rounds", type=int, default=3,
                        help="number of DID query rounds (default: 3)")
    parser.add_argument("--protocol", choices=("6", "0"), default="6",
                        help="ELM protocol: 6=CAN 11-bit/500k; 0=auto (default: 6)")
    parser.add_argument("--response-timeout", type=float, default=3.0,
                        help="seconds to wait for each adapter response")
    args = parser.parse_args()

    if args.rounds < 1 or args.rounds > 20:
        parser.error("--rounds must be between 1 and 20")
    if args.response_timeout < 0.5 or args.response_timeout > 30:
        parser.error("--response-timeout must be between 0.5 and 30 seconds")

    args.output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    stem = f"ford_tpms_{stamp}"
    transcript_path = args.output / f"{stem}.txt"
    json_path = args.output / f"{stem}.json"
    raw_path = args.output / f"{stem}_uart.bin"

    transcript: list[str] = []
    all_records: list[dict[str, Any]] = []

    def note(message: str) -> None:
        print(message)
        transcript.append(message)

    capture_started_local = local_timestamp()
    capture_started_utc = utc_timestamp()
    probe_records: list[dict[str, Any]] = []

    note("Ford TPMS Mode 22 capture")
    note(f"Started local: {capture_started_local}")
    note(f"Started UTC: {capture_started_utc}")
    note(f"Host: {platform.platform()}")
    note(f"Python: {sys.version.split()[0]}")
    note(f"Candidates: {', '.join(PORT_CANDIDATES)}")
    note("Probe command: ATZ only; vehicle requests are read-only service 22.")
    note("This is ELM UART I/O logging, not a raw/passive CAN bus capture.")
    note("")

    listed = list_candidate_ports()
    for item in listed:
        note(f"Port listed: {item['device']} | {item.get('description', '')} | "
             f"{item.get('manufacturer', '')} | {item.get('hwid', '')}")

    if args.port == "auto":
        ports_to_probe = list(PORT_CANDIDATES)
    else:
        ports_to_probe = [args.port]

    selected_port = None
    for device in ports_to_probe:
        note(f"Probing {device} at {BAUDRATE} baud using ATZ...")
        ok, result = probe_port(device, args.response_timeout)
        result["timestamp_local"] = local_timestamp()
        probe_records.append(result)
        note(f"Probe TX {device}: {result['tx_hex']}")
        note(f"Probe RX {device} HEX: {result['rx_hex']}")
        note(f"Probe RX {device} ASCII: {result['rx_ascii']!r}")
        if result.get("error"):
            note(f"Probe error {device}: {result['error']}")
        if ok:
            selected_port = device
            note(f"Selected adapter port: {device}")
            break
    if not selected_port:
        note("ERROR: no ELM-like adapter responded on the selected COM port(s).")
        note("Check Windows Device Manager, close FORScan, and verify Bluetooth SPP pairing.")
        transcript_path.write_text("\n".join(transcript) + "\n", encoding="utf-8")
        write_json(json_path, {
            "schema": "ford_tpms_capture_v1",
            "selected_port": None,
            "ports": listed,
            "probe_results": probe_records,
            "started_local": capture_started_local,
            "started_utc": capture_started_utc,
            "exchanges": [],
        })
        return 3

    setup_commands = [
        "ATZ",
        "ATI",
        "ATE0",
        "ATL0",
        "ATS1",
        "ATH1",
        f"ATSP{args.protocol}",
        "ATCAF1",
        "ATDP",
        "ATDPN",
        "ATRV",
        "ATSH726",
    ]
    try:
        with serial.Serial(
            port=selected_port,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            write_timeout=2,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as port:
            time.sleep(0.3)
            for command in setup_commands:
                record = exchange(port, command, args.response_timeout)
                record["kind"] = "adapter_setup"
                all_records.append(record)
                transcript.append(
                    f"[{record['timestamp_local']}] TX {command!r} "
                    f"({record['tx_hex']})\n"
                    f"RX HEX: {record['rx_hex']}\n"
                    f"RX ASCII: {record['rx_ascii']!r}\n"
                    f"elapsed_ms={record['elapsed_ms']} timed_out={record['timed_out']}"
                )
                print(transcript[-1])

            for round_index in range(1, args.rounds + 1):
                for wheel, command in DID_REQUESTS:
                    record = exchange(port, command, args.response_timeout)
                    record.update({
                        "kind": "uds_read",
                        "round": round_index,
                        "wheel_candidate": wheel,
                        "ecu_header_candidate": "726",
                        "service": "22",
                        "did": command[-4:].upper(),
                    })
                    all_records.append(record)
                    transcript.append(
                        f"[{record['timestamp_local']}] ROUND {round_index} {wheel} "
                        f"TX {command!r} ({record['tx_hex']})\n"
                        f"RX HEX: {record['rx_hex']}\n"
                        f"RX ASCII: {record['rx_ascii']!r}\n"
                        f"elapsed_ms={record['elapsed_ms']} timed_out={record['timed_out']}"
                    )
                    print(transcript[-1])
                    time.sleep(0.15)
    except (serial.SerialException, OSError) as exc:
        note(f"ERROR during capture: {type(exc).__name__}: {exc}")

    # Concatenate exact UART TX/RX byte strings with metadata separators so
    # captures can be re-parsed without relying on terminal rendering.
    with raw_path.open("wb") as raw_file:
        for index, record in enumerate(all_records, start=1):
            raw_file.write(f"\n--- RECORD {index} {record['timestamp_local']} "
                           f"{record['command']} ---\n".encode("ascii"))
            raw_file.write(bytes.fromhex(record["tx_hex"]))
            raw_file.write(b"\n< RX >\n")
            raw_file.write(bytes.fromhex(record["rx_hex"]))
            raw_file.write(b"\n")

    transcript_path.write_text("\n\n".join(transcript) + "\n", encoding="utf-8")
    write_json(json_path, {
        "schema": "ford_tpms_capture_v1",
        "started_local": capture_started_local,
        "started_utc": capture_started_utc,
        "selected_port": selected_port,
        "baudrate": BAUDRATE,
        "serial": {"bytesize": 8, "parity": "N", "stopbits": 1,
                   "flow_control": "none"},
        "vehicle_context": {
            "reported_vehicle": "2014 Ford Mondeo 2.0T (FORScan file identified 2013 MY)",
            "ecu_candidate": "BCMii",
            "ecu_header_candidate": "726",
            "protocol_candidate": args.protocol,
            "did_wheel_candidates": {wheel: command[-4:] for wheel, command in DID_REQUESTS},
        },
        "capture_semantics": {
            "raw_uart_file": raw_path.name,
            "note": "Exact bytes exchanged with ELM adapter, not raw CAN bus frames.",
        },
        "probe_results": probe_records,
        "exchanges": all_records,
    })
    note("")
    note(f"Saved transcript: {transcript_path.resolve()}")
    note(f"Saved structured log: {json_path.resolve()}")
    note(f"Saved UART byte log: {raw_path.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
