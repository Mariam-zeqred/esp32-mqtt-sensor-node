// esp32-mqtt-sensor-node: read an SHT31 every SAMPLE_PERIOD_S and publish JSON over MQTT/TLS.
//
// Flow:  boot -> Wi-Fi -> NTP -> MQTT (with last will) -> [read -> publish] every period
//        DEEP_SLEEP_ENABLED=1: after one publish, say "sleeping", disconnect cleanly, deep-sleep.
//        DEEP_SLEEP_ENABLED=0: stay connected; Wi-Fi and MQTT reconnect with exponential backoff.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "Backoff.h"
#include "Config.h"
#include "Telemetry.h"
#include "mqtt_link.h"
#include "net.h"
#include "sensor.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

RTC_DATA_ATTR static uint32_t g_seq = 0;  // survives deep sleep, not power loss
static char g_node_id[16];
static Backoff g_wifi_backoff(BACKOFF_BASE_MS, BACKOFF_CAP_MS, BACKOFF_JITTER_PCT, esp_random);
static Backoff g_mqtt_backoff(BACKOFF_BASE_MS, BACKOFF_CAP_MS, BACKOFF_JITTER_PCT, esp_random);
static uint32_t g_last_sample_ms = 0;
static uint32_t g_sensor_failures = 0;

static bool sample_and_publish() {
  float t, h;
  bool ok = sensor::read(t, h);
  g_sensor_failures = ok ? 0 : g_sensor_failures + 1;
  if (SENSOR_FAIL_REBOOT_LIMIT && g_sensor_failures >= SENSOR_FAIL_REBOOT_LIMIT) {
    Serial.println("[node] sensor failed repeatedly, restarting");
    ESP.restart();
  }

  Telemetry rec;
  rec.node_id = g_node_id;
  rec.ts = net::now_unix();
  rec.seq = g_seq++;
  rec.temp_c = t;  // NaN -> null when the read failed: the gap itself is information
  rec.rh_pct = h;
  rec.rssi_dbm = WiFi.RSSI();
  rec.uptime_s = millis() / 1000;
  rec.fw = FW_VERSION;

  char json[MQTT_BUFFER_BYTES - 64];
  if (telemetry_to_json(rec, json, sizeof json) == 0) {
    Serial.println("[node] JSON did not fit, not publishing");
    return false;
  }
  return mqtt::publish_telemetry(json);
}

static void deep_sleep_cycle() {
  if (mqtt::connected()) {
    mqtt::publish_status("sleeping");  // intentional absence, distinct from the "offline" last will
    mqtt::loop();
    delay(100);  // let the packets leave before the radio goes down
    mqtt::disconnect();
  }
  WiFi.disconnect(true);
  Serial.printf("[node] deep sleep for %d s\n", SAMPLE_PERIOD_S);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)SAMPLE_PERIOD_S * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  node_id_from_mac(mac, g_node_id, sizeof g_node_id);
  Serial.printf("\n[node] %s fw %s, period %d s, deep sleep %s\n", g_node_id, FW_VERSION, SAMPLE_PERIOD_S,
                DEEP_SLEEP_ENABLED ? "on" : "off");

  sensor::begin();  // a missing sensor still publishes null readings, so the gap is visible
  mqtt::begin(g_node_id);

  if (net::wifi_connect()) {
    net::time_sync(NTP_SYNC_TIMEOUT_MS);  // TLS needs a sane clock to validate the certificate
  }
}

void loop() {
  uint32_t now = millis();
  bool online = net::wifi_ensure(g_wifi_backoff, now) && mqtt::ensure(g_mqtt_backoff, now);
  if (online) {
    mqtt::loop();
    if (!net::time_is_valid()) net::time_sync(1000);
  }

#if DEEP_SLEEP_ENABLED
  if (online) {
    sample_and_publish();
    deep_sleep_cycle();
  } else if (now > WIFI_CONNECT_TIMEOUT_MS + BACKOFF_CAP_MS) {
    deep_sleep_cycle();  // do not drain the battery retrying forever; try again next period
  }
#else
  if (online && now - g_last_sample_ms >= (uint32_t)SAMPLE_PERIOD_S * 1000UL) {
    g_last_sample_ms = now;
    sample_and_publish();
  }
#endif
  delay(10);
}
