// Telemetry record, JSON serialisation, topic and node-id helpers.
// Pure C/C++ with fixed buffers and no heap, so the same code runs in the firmware
// and in the host unit tests.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct Telemetry {
  const char* node_id;  // e.g. "esp32-a1b2c3"
  int64_t ts;           // Unix seconds from NTP; 0 means "clock not synced" -> JSON null
  uint32_t seq;         // reading counter, survives deep sleep (RTC memory)
  float temp_c;         // NaN -> JSON null
  float rh_pct;         // NaN -> JSON null
  int rssi_dbm;         // Wi-Fi RSSI
  uint32_t uptime_s;    // seconds since boot
  const char* fw;       // firmware version string
};

// Writes the JSON document for one reading into buf.
// Returns the number of bytes written (excluding the terminating NUL),
// or 0 if buf is too small - the caller must not publish a truncated document.
size_t telemetry_to_json(const Telemetry& t, char* buf, size_t len);

// Status document for the retained "<prefix>/<node>/status" topic and the last will.
// ts == 0 omits the timestamp (the last will is registered before NTP has synced).
size_t status_to_json(const char* node_id, const char* state, int64_t ts, char* buf, size_t len);

// SHT31 datasheet operating range; anything outside is a wiring/bus fault, not weather.
bool reading_is_plausible(float temp_c, float rh_pct);

// "<prefix>/<node_id>/<leaf>", e.g. "farm/esp32-a1b2c3/telemetry". Returns 0 on overflow.
size_t make_topic(char* buf, size_t len, const char* prefix, const char* node_id, const char* leaf);

// "esp32-" + the last three MAC bytes in lower-case hex, e.g. "esp32-a1b2c3". out >= 13 bytes.
void node_id_from_mac(const uint8_t mac[6], char* out, size_t len);
