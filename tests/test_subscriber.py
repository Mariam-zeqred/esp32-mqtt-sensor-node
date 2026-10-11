"""Unit tests for tools/subscriber.py (no broker needed)."""

import csv
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import subscriber  # noqa: E402

# The exact document the firmware's JSON builder produces (see test/test_native/test_core.cpp).
FIRMWARE_DOC = (
    b'{"node":"esp32-a1b2c3","ts":1791676800,"seq":42,'
    b'"temp_c":36.52,"rh_pct":61.25,"rssi_dbm":-67,"uptime_s":1234,"fw":"0.1.0"}'
)


def test_parse_firmware_document() -> None:
    row = subscriber.parse_message("farm/esp32-a1b2c3/telemetry", FIRMWARE_DOC, 1791676801.0)
    assert list(row) == subscriber.COLUMNS
    assert row["received_utc"] == "2026-10-11T00:00:01+00:00"
    assert row["node"] == "esp32-a1b2c3"
    assert row["ts"] == 1791676800
    assert row["seq"] == 42
    assert row["temp_c"] == pytest.approx(36.52)
    assert row["rh_pct"] == pytest.approx(61.25)
    assert row["rssi_dbm"] == -67
    assert row["fw"] == "0.1.0"
    assert row["state"] == "" and row["raw"] == ""


def test_nulls_become_blank_and_unknown_keys_are_ignored() -> None:
    doc = b'{"node":"esp32-a1b2c3","ts":null,"seq":7,"temp_c":null,"rh_pct":null,"extra":1}'
    row = subscriber.parse_message("farm/esp32-a1b2c3/telemetry", doc, 0.0)
    assert row["ts"] == "" and row["temp_c"] == "" and row["rh_pct"] == ""
    assert row["seq"] == 7
    assert "extra" not in row


def test_status_document() -> None:
    row = subscriber.parse_message("farm/esp32-a1b2c3/status", b'{"node":"esp32-a1b2c3","state":"offline"}', 0.0)
    assert row["state"] == "offline"
    assert "state=offline" in subscriber.format_line(row)


def test_non_json_payload_is_kept_raw() -> None:
    row = subscriber.parse_message("farm/x/telemetry", b"\xff\xfenot json", 0.0)
    assert row["raw"].endswith("not json")
    assert row["node"] == ""
    assert "RAW" in subscriber.format_line(row)
    row = subscriber.parse_message("farm/x/telemetry", b"[1,2,3]", 0.0)
    assert row["raw"] == "[1,2,3]"


def test_csv_log_writes_header_once(tmp_path: Path) -> None:
    path = tmp_path / "sub" / "t.csv"
    row = subscriber.parse_message("farm/esp32-a1b2c3/telemetry", FIRMWARE_DOC, 1791676801.0)
    log = subscriber.CsvLog(path)
    log.write(row)
    log.close()
    log = subscriber.CsvLog(path)  # reopen: must append, not repeat the header
    log.write(row)
    log.close()
    with path.open(newline="") as fh:
        rows = list(csv.DictReader(fh))
    assert len(rows) == 2
    assert rows[0]["temp_c"] == "36.52" and rows[1]["seq"] == "42"
    assert path.read_text().count("received_utc") == 1


def test_load_dotenv(tmp_path: Path) -> None:
    env_file = tmp_path / ".env"
    env_file.write_text("# comment\nMQTT_HOST=broker.local\nMQTT_PORT = 8883\nMQTT_PASS='a=b'\nbad line\n")
    env = subscriber.load_dotenv(env_file)
    assert env == {"MQTT_HOST": "broker.local", "MQTT_PORT": "8883", "MQTT_PASS": "a=b"}
    assert subscriber.load_dotenv(tmp_path / "missing") == {}


def test_parser_defaults_from_env() -> None:
    args = subscriber.build_parser({"MQTT_HOST": "h", "MQTT_PORT": "1", "MQTT_USER": "u"}).parse_args([])
    assert (args.host, args.port, args.user, args.passphrase) == ("h", 1, "u", None)
    assert args.csv == Path("data/telemetry.csv")
