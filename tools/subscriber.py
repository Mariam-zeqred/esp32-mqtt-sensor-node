#!/usr/bin/env python3
"""Subscribe to the sensor node's topics over TLS, print each message and append it to a CSV.

    python tools/subscriber.py --host 192.168.1.10 --ca certs/ca.crt --user node --pass ...
    python tools/subscriber.py            # reads MQTT_* from .env / the environment

Columns are fixed so the CSV stays loadable with pandas even when a field is missing.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import ssl
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt

COLUMNS = [
    "received_utc",
    "topic",
    "node",
    "ts",
    "seq",
    "temp_c",
    "rh_pct",
    "rssi_dbm",
    "uptime_s",
    "fw",
    "state",
    "raw",
]


def parse_message(topic: str, payload: bytes, received: float) -> dict[str, Any]:
    """Flatten one MQTT message into a CSV row.

    Unknown JSON keys are ignored, missing ones are blank, JSON null becomes blank.
    A payload that is not a JSON object is kept verbatim in the `raw` column so nothing is lost.
    """
    row: dict[str, Any] = {c: "" for c in COLUMNS}
    row["received_utc"] = datetime.fromtimestamp(received, UTC).isoformat(timespec="seconds")
    row["topic"] = topic
    try:
        doc = json.loads(payload)
    except (ValueError, UnicodeDecodeError):
        doc = None
    if not isinstance(doc, dict):
        row["raw"] = payload.decode("utf-8", errors="replace")
        return row
    for key in COLUMNS[2:-1]:
        value = doc.get(key)
        row[key] = "" if value is None else value
    return row


def format_line(row: dict[str, Any]) -> str:
    """One console line per message."""
    if row["raw"]:
        return f"{row['received_utc']} {row['topic']} RAW {row['raw']}"
    if row["state"]:
        return f"{row['received_utc']} {row['topic']} state={row['state']}"
    return (
        f"{row['received_utc']} {row['topic']} seq={row['seq']} "
        f"temp_c={row['temp_c']} rh_pct={row['rh_pct']} rssi={row['rssi_dbm']}"
    )


class CsvLog:
    """Append-only CSV writer that writes the header once for a new or empty file."""

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        new_file = not path.exists() or path.stat().st_size == 0
        self._fh = path.open("a", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(self._fh, fieldnames=COLUMNS)
        if new_file:
            self._writer.writeheader()
            self._fh.flush()

    def write(self, row: dict[str, Any]) -> None:
        self._writer.writerow(row)
        self._fh.flush()

    def close(self) -> None:
        self._fh.close()


def load_dotenv(path: Path) -> dict[str, str]:
    """Tiny .env reader (KEY=VALUE, # comments); avoids a dependency for five variables."""
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip().strip("'\"")
    return values


def build_parser(env: dict[str, str]) -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default=env.get("MQTT_HOST", "localhost"))
    p.add_argument("--port", type=int, default=int(env.get("MQTT_PORT", "8883")))
    p.add_argument("--user", default=env.get("MQTT_USER"))
    p.add_argument("--pass", dest="passphrase", default=env.get("MQTT_PASS"))
    p.add_argument("--ca", default=env.get("MQTT_CA", "certs/ca.crt"), help="root CA that signed the broker cert")
    p.add_argument("--prefix", default="farm", help="topic prefix, same as TOPIC_PREFIX in Config.h")
    p.add_argument("--csv", type=Path, default=Path("data/telemetry.csv"))
    p.add_argument("--count", type=int, default=0, help="exit after N messages (0 = run forever)")
    p.add_argument("--insecure", action="store_true", help="skip hostname check (debug only)")
    return p


def main(argv: list[str] | None = None) -> int:
    env = {**load_dotenv(Path(".env")), **os.environ}
    args = build_parser(env).parse_args(argv)
    log = CsvLog(args.csv)
    received_count = 0

    def on_connect(client: mqtt.Client, _userdata: Any, _flags: Any, reason_code: Any, _props: Any = None) -> None:
        if reason_code != 0:
            print(f"connect failed: {reason_code}", file=sys.stderr)
            return
        for leaf in ("telemetry", "status"):
            client.subscribe(f"{args.prefix}/+/{leaf}", qos=1)
        print(f"connected to {args.host}:{args.port}, logging to {args.csv}")

    def on_message(client: mqtt.Client, _userdata: Any, msg: mqtt.MQTTMessage) -> None:
        nonlocal received_count
        row = parse_message(msg.topic, msg.payload, datetime.now(UTC).timestamp())
        log.write(row)
        print(format_line(row), flush=True)
        received_count += 1
        if args.count and received_count >= args.count:
            client.disconnect()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv311)
    client.tls_set(ca_certs=args.ca, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.tls_insecure_set(args.insecure)
    if args.user:
        client.username_pw_set(args.user, args.passphrase)
    client.on_connect = on_connect
    client.on_message = on_message
    try:
        client.connect(args.host, args.port, keepalive=30)
        client.loop_forever()
    except KeyboardInterrupt:
        pass
    finally:
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
