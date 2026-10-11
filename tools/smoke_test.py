#!/usr/bin/env python3
"""End-to-end check on the host: TLS broker -> subscriber -> CSV.

Starts an in-process amqtt broker on 127.0.0.1:18883 with the certificates from
certs/ (run tools/make_certs.sh 127.0.0.1 first), runs tools/subscriber.py as a
subprocess, publishes three documents exactly as the firmware would, and checks
the CSV it wrote. This exercises the certificate chain, the topic layout, the
retained status message and the CSV logger - everything except the ESP32 itself.
"""

from __future__ import annotations

import asyncio
import csv
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import paho.mqtt.client as mqtt
from amqtt.broker import Broker

ROOT = Path(__file__).resolve().parents[1]
PORT = 18883
NODE = "esp32-a1b2c3"
DOCS = [
    (f"farm/{NODE}/status", b'{"node":"esp32-a1b2c3","state":"online","ts":1791676800}', True),
    (
        f"farm/{NODE}/telemetry",
        b'{"node":"esp32-a1b2c3","ts":1791676800,"seq":42,'
        b'"temp_c":36.52,"rh_pct":61.25,"rssi_dbm":-67,"uptime_s":1234,"fw":"0.1.0"}',
        False,
    ),
    (
        f"farm/{NODE}/telemetry",
        b'{"node":"esp32-a1b2c3","ts":null,"seq":43,"temp_c":null,"rh_pct":null,"rssi_dbm":-70,"uptime_s":1264,"fw":"0.1.0"}',
        False,
    ),
]


def broker_config() -> dict:
    return {
        "listeners": {
            "default": {
                "type": "tcp",
                "bind": f"127.0.0.1:{PORT}",
                "ssl": True,
                "certfile": str(ROOT / "certs/server.crt"),
                "keyfile": str(ROOT / "certs/server.key"),
            }
        },
        "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}},
    }


def publish_all() -> None:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=NODE, protocol=mqtt.MQTTv311)
    client.tls_set(ca_certs=str(ROOT / "certs/ca.crt"), tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.connect("127.0.0.1", PORT)
    client.loop_start()
    for topic, payload, retain in DOCS:
        client.publish(topic, payload, qos=1, retain=retain).wait_for_publish(timeout=5)
    client.loop_stop()
    client.disconnect()


async def run() -> int:
    for f in ("certs/ca.crt", "certs/server.crt", "certs/server.key"):
        if not (ROOT / f).exists():
            print(f"missing {f}: run tools/make_certs.sh 127.0.0.1 first")
            return 2
    broker = Broker(broker_config())
    await broker.start()
    print(f"broker up on 127.0.0.1:{PORT} (TLS)")
    try:
        with tempfile.TemporaryDirectory() as tmp:
            csv_path = Path(tmp) / "telemetry.csv"
            sub = subprocess.Popen(
                [
                    sys.executable,
                    str(ROOT / "tools/subscriber.py"),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(PORT),
                    "--ca",
                    str(ROOT / "certs/ca.crt"),
                    "--csv",
                    str(csv_path),
                    "--count",
                    str(len(DOCS)),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            await asyncio.sleep(1.5)  # let it connect and subscribe
            await asyncio.get_running_loop().run_in_executor(None, publish_all)
            deadline = time.time() + 10
            while sub.poll() is None and time.time() < deadline:
                await asyncio.sleep(0.2)
            if sub.poll() is None:
                sub.kill()
                print("subscriber did not finish in time\n" + (sub.communicate()[0] or ""))
                return 1
            out = sub.communicate()[0]
            print(out.rstrip())
            with csv_path.open(newline="") as fh:
                rows = list(csv.DictReader(fh))
    finally:
        await broker.shutdown()

    ok = len(rows) == len(DOCS)
    ok &= rows[0]["state"] == "online" and rows[0]["ts"] == "1791676800"
    ok &= rows[1]["seq"] == "42" and rows[1]["temp_c"] == "36.52" and rows[1]["rh_pct"] == "61.25"
    ok &= rows[2]["seq"] == "43" and rows[2]["temp_c"] == "" and rows[2]["ts"] == ""
    print(f"CSV rows: {len(rows)} -> {'OK' if ok else 'MISMATCH'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(run()))
