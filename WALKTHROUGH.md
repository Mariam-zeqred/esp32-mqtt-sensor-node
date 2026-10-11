# Walkthrough (for Mariam)

## Why it is built this way

This is the smallest honest version of "device to cloud": a sensor, a secure transport, a
broker you control, and a consumer that writes rows you can analyse. Everything that can be
reasoned about without a board lives in `lib/node_core/` and is tested on the host, which is
how firmware logic should be tested anyway: the JSON builder and the backoff policy do not
need an ESP32 to be wrong.

The MQTT details are the point. A retained status topic plus a last will is the standard
"is the device alive?" pattern, and the broker does the work. The `sleeping` state makes an
intentional absence look different from a failure. Backoff with jitter is what you need once
twenty nodes sit behind one router. TLS with your own CA shows that "encrypted" means nothing
unless the client checks who it is talking to, which is why `MQTT_HOST` must match the
certificate's name.

## File map

- `platformio.ini` — `esp32dev` embeds `certs/ca.crt` into the binary
  (`board_build.embed_txtfiles`), which is how `mqtt_link.cpp` gets the CA. `native` builds
  only `lib/` and `test/` with the host compiler.
- `include/Config.h` — every tuning knob; `secrets.example.h` is the template for `secrets.h`.
- `lib/node_core/Telemetry.cpp` — `snprintf` JSON that refuses to truncate, the SHT31 range
  check, node names from the MAC. `Backoff.cpp` — the retry policy with an injectable random
  source so tests are deterministic.
- `src/net.cpp` — Wi-Fi and NTP; `wifi_ensure` is non-blocking so the loop keeps running.
- `src/mqtt_link.cpp` — connect with the will registered, status on connect, clean disconnect
  before deep sleep.
- `src/sensor.cpp` — the only file that knows it is an SHT31. `src/main.cpp` — the schedule.
- `tools/subscriber.py` — fixed CSV columns so a missing field never shifts a column.
  `tools/smoke_test.py` — a real TLS broker in-process plus the subscriber and a CSV check.

## Three questions a professor may ask

**1. Why a last will instead of a heartbeat the server watches?** It is simpler and needs no
server-side timer: the broker already tracks the keepalive (30 s; dead after 1.5×). The
weakness is that a will fires only when the TCP session dies, so a node stuck in a loop still
looks online. That is why `seq` and `uptime_s` are in every document: a consumer can see
"connected but not publishing", which the status topic cannot show.

**2. Why exponential backoff with jitter rather than a fixed 5 s retry?** Fixed retries hammer a
broker that is restarting and synchronise every node that lost it at the same moment, so the
TLS handshakes pile up. Doubling spreads the load, the 60 s cap keeps recovery short, and the
±20 % jitter decorrelates the nodes. `reset()` after success matters too: without it a node up
for a week would wait a minute after a one-second glitch.

**3. What does TLS protect here, and what does it not?** It protects the readings in transit
and, since the node checks the broker certificate and hostname against the embedded CA, stops a
rogue broker from harvesting credentials. It does not authenticate the node strongly:
the password is shared, so a leaked `secrets.h` lets anyone publish as any node. The fix is
per-device client certificates (mutual TLS: Mosquitto's `require_certificate true` and
`use_identity_as_username true`). TLS also needs the right time, hence NTP before connecting.

## One thing to extend

An offline ring buffer: keep the last N readings in RTC memory when publishing fails and
flush them in order on reconnect. `seq` already lets the subscriber prove nothing was lost.

## Verified and not verified

Run in the build session: `make check`, `pio test -e native`, `make smoke`. Not run: the
ESP32 compile and a flash (the sandbox could not reach the PlatformIO registry); the library
calls were checked against the current library headers and the firmware syntax-checked with
stubs. First thing on your machine: `pio run -e esp32dev`, or check CI's `firmware-build` job.
