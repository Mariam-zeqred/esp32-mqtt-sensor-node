# esp32-mqtt-sensor-node

An ESP32 reads an SHT31 temperature/humidity sensor every 30 s and publishes one JSON
document per reading to an MQTT broker over TLS. The node announces itself with a retained
`online` status and registers a last will so the broker publishes `offline` when the node
vanishes. This repo shows the whole path: firmware (Arduino-ESP32, PlatformIO), a Mosquitto
broker in Docker with its own certificate authority, a Python subscriber that logs to CSV,
and host-side tests for everything that does not need the board.

```
 SHT31 ──I2C──> ESP32 ──Wi-Fi──> MQTT/TLS 8883 ──> Mosquitto (Docker) ──> tools/subscriber.py ──> CSV
                  │                                      ▲
                  │ every 30 s: farm/<node>/telemetry    │ retained: farm/<node>/status
                  │ {"node","ts","seq","temp_c",...}     │ "online" | "sleeping" | "offline" (last will)
                  └── NTP (pool.ntp.org) for ts and for TLS certificate validation
```

## Hardware

| SHT31 breakout | ESP32 DevKit v1 |
|---|---|
| VIN | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| ADR | GND (address 0x44; 3V3 gives 0x45, set `SHT31_ADDR` in `include/Config.h`) |

A BME280 works with the same structure: swap `src/sensor.cpp` and the library in `platformio.ini`.

## Run it

Needs Python 3.11+, Docker, PlatformIO (`pip install platformio`), and the broker machine's LAN IP (say `192.168.1.10`).

```bash
pip install -r requirements-dev.txt
tools/make_certs.sh 192.168.1.10                  # private CA + broker cert (certs/)
tools/broker_setup.sh node <passphrase>           # Mosquitto password file (docker/passwd)
docker compose up -d                              # broker on :8883, TLS only
cp include/secrets.example.h include/secrets.h    # edit: Wi-Fi, MQTT_HOST=192.168.1.10, MQTT_PASS
pio run -e esp32dev -t upload && pio device monitor
python tools/subscriber.py --host 192.168.1.10 --user node --pass <passphrase>
```

`MQTT_HOST` must be exactly the name or IP you passed to `make_certs.sh`: the ESP32 verifies the
broker certificate against the embedded `certs/ca.crt` **and** checks the hostname.

## What you see

The subscriber prints one line per message and appends to `data/telemetry.csv`. This is the
real output of `make smoke`, which runs a TLS broker, the subscriber and a publisher on one
machine with the exact documents the firmware's JSON builder produces (see the unit tests):

```
broker up on 127.0.0.1:18883 (TLS)
connected to 127.0.0.1:18883, logging to /tmp/tmpmbml7lo0/telemetry.csv
2026-10-11T02:52:39+00:00 farm/esp32-a1b2c3/status state=online
2026-10-11T02:52:39+00:00 farm/esp32-a1b2c3/telemetry seq=42 temp_c=36.52 rh_pct=61.25 rssi=-67
2026-10-11T02:52:39+00:00 farm/esp32-a1b2c3/telemetry seq=43 temp_c= rh_pct= rssi=-70
CSV rows: 3 -> OK
```

One telemetry document (`ts` is Unix UTC seconds; `null` means the clock was not yet synced or
the sensor read failed, so a gap is visible rather than silently skipped):

```json
{"node":"esp32-a1b2c3","ts":1791676800,"seq":42,"temp_c":36.52,"rh_pct":61.25,"rssi_dbm":-67,"uptime_s":1234,"fw":"0.1.0"}
```

Status documents on the retained topic `farm/<node>/status`: `{"node":"...","state":"online","ts":...}`
on connect, `"sleeping"` before an intentional deep sleep, and `{"node":"...","state":"offline"}`
sent by the broker as the last will when the keepalive (30 s) lapses without a clean disconnect.

## Design notes

- **Last will vs. clean disconnect.** `offline` is only ever written by the broker, so it means
  "the node disappeared" (power cut, Wi-Fi loss, crash). A node that goes to deep sleep first
  publishes `sleeping` and disconnects cleanly, so the two cases are distinguishable from the
  status topic alone.
- **Reconnect with exponential backoff and jitter.** Wi-Fi and MQTT each use `Backoff`
  (1 s, 2 s, 4 s ... capped at 60 s, ±20 % jitter). Jitter stops a fleet that lost the broker at
  the same moment from reconnecting in the same second. `reset()` after success.
- **No heap, no JSON library.** `telemetry_to_json` writes with `snprintf` into a fixed buffer and
  returns 0 if the document would not fit; the node never publishes a truncated document.
- **TLS needs a clock.** Certificate validity is checked against the system time, so NTP runs
  before the first MQTT connect. If NTP has not synced within 10 s the node tries to connect
  anyway and publishes `ts:null`; whether mbedTLS accepts the certificate while the clock still
  reads 1970 is untested on hardware (it may reject it as "not yet valid" until SNTP, which
  keeps running in the background, sets the time).
- **Deep sleep option.** `DEEP_SLEEP_ENABLED 1` in `Config.h`: one reading per wake, `seq` kept
  in RTC memory, radio off between readings. If it cannot connect within about 75 s it sleeps
  anyway instead of draining the battery.
- **Sensor faults are data.** An implausible reading (outside the SHT31's -40..125 °C /
  0..100 % range, or NaN) is published as `null`; ten consecutive failures restart the node.

## Tests and checks

| Command | What it proves | Run here |
|---|---|---|
| `make test-core` | 272 assertions on the JSON builder, status/last-will documents, plausibility range, topic/node-id helpers and backoff (plain g++, no downloads) | yes, passing |
| `pio test -e native` | the same tests through PlatformIO's native environment and the custom runner in `test/` | yes, passing |
| `make test-py` | 7 pytest tests for `tools/subscriber.py` (parsing, nulls, raw payloads, CSV header, `.env`) | yes, passing |
| `make smoke` | TLS broker (amqtt) → subscriber → CSV, end to end on one machine | yes, passing |
| `make lint` | `ruff check` + `ruff format --check` | yes, passing |
| `pio run -e esp32dev` | the firmware compiles | not in the build sandbox (no package registry access); **passing** in the `firmware-build` job of `.github/workflows/ci.yml` |

`make check` = lint + core tests + Python tests. The firmware compiles in CI (espressif32 6.x,
Arduino core 2.0.x) but has not been flashed to a board yet.

## Layout

```
platformio.ini            esp32dev (firmware) and native (host tests) environments
include/Config.h          every tuning knob; include/secrets.example.h -> secrets.h (git-ignored)
lib/node_core/            Telemetry.{h,cpp} JSON/topics/plausibility, Backoff.{h,cpp}  (no Arduino)
src/main.cpp              setup/loop, sampling schedule, deep sleep
src/net.cpp  mqtt_link.cpp  sensor.cpp     Wi-Fi + NTP, MQTT/TLS + last will, SHT31
test/test_native/         host unit tests; test/test_custom_runner.py for `pio test`
tools/                    make_certs.sh, broker_setup.sh, subscriber.py, smoke_test.py
docker/ + docker-compose.yml   Mosquitto 2 with TLS listener and password file
tests/                    pytest for the subscriber
```

## Limitations

- Compiles in CI, not yet run on hardware. The Mosquitto/Docker path (`docker-compose.yml`,
  `make_certs.sh`, `broker_setup.sh`) has not been run either; `make smoke` uses an in-process
  Python broker instead.
- One sensor type, one broker, no OTA, no message buffering while offline (readings taken
  while disconnected are lost, which `seq` makes visible).
- Server-side TLS only. Client certificates would be the next step for per-device identity;
  with username/password every node shares the same credential.
- Deep-sleep current was not measured; the ~75 s give-up time is a parameter, not a tuned value.

## Next steps

Client certificates per node, a ring buffer in RTC memory for readings taken while offline,
and a Grafana dashboard reading the CSV or a small SQLite sink. The firmware structure
(`net` / `mqtt_link` / `sensor` modules plus a hardware-free core) is the same one the
[queen-bee-incubator](https://github.com/Mariam-zeqred/queen-bee-incubator) and
[smart-poultry-farm](https://github.com/Mariam-zeqred/smart-poultry-farm) controllers would
use to move from Firebase to a broker they own.

MIT licence.
