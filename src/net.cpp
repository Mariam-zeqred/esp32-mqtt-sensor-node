#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "Config.h"
#include "secrets.h"

namespace net {

static uint32_t next_wifi_attempt_ms = 0;

bool wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // we own the retry policy
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, ip=%s rssi=%d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.printf("[wifi] connect failed, status=%d\n", (int)WiFi.status());
  return false;
}

bool wifi_ensure(Backoff& backoff, uint32_t now_ms) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if ((int32_t)(now_ms - next_wifi_attempt_ms) < 0) return false;  // not yet
  WiFi.disconnect(true);
  if (wifi_connect()) {
    backoff.reset();
    return true;
  }
  uint32_t wait = backoff.next();
  next_wifi_attempt_ms = now_ms + wait;
  Serial.printf("[wifi] retry #%lu in %lu ms\n", (unsigned long)backoff.attempts(), (unsigned long)wait);
  return false;
}

bool time_is_valid() { return time(nullptr) > (time_t)TIME_VALID_AFTER; }

bool time_sync(uint32_t timeout_ms) {
  if (time_is_valid()) return true;
  configTime(0, 0, NTP_SERVER);  // UTC; local time is the dashboard's problem
  uint32_t start = millis();
  while (!time_is_valid() && millis() - start < timeout_ms) delay(100);
  if (time_is_valid()) {
    Serial.printf("[ntp] synced, unix=%lld\n", (long long)time(nullptr));
    return true;
  }
  Serial.println("[ntp] not synced yet, publishing ts:null");
  return false;
}

int64_t now_unix() { return time_is_valid() ? (int64_t)time(nullptr) : 0; }

}  // namespace net
