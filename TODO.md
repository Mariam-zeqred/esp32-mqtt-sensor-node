# TODO

## Open
- Flash a board and verify the NTP -> TLS order: `mqtt::ensure` may run before SNTP has set the clock, and mbedTLS can reject the broker certificate as "not yet valid" while the time reads 1970. If it does, make the first MQTT connect wait for `net::time_is_valid()` (or a bounded timeout) and update the README "TLS needs a clock" note. (QC 2026-10-11)
- Run the Docker broker path end to end once (`tools/make_certs.sh`, `tools/broker_setup.sh`, `docker compose up -d`, `tools/subscriber.py` against Mosquitto, not amqtt); it has never been executed, only read. (QC 2026-10-11)
- `pio test -e native` cannot run in the cloud sandbox (PlatformIO registry unreachable: `HTTPClientError` while installing the `native` platform); it is covered by the `host-checks` CI job, which is green. (QC 2026-10-11)
