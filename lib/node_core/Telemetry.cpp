#include "Telemetry.h"

#include <math.h>
#include <stdio.h>

namespace {

// Appends a float as a JSON number with two decimals, or null if it is not finite.
int put_float(char* buf, size_t len, const char* key, float v) {
  if (isnan(v) || isinf(v)) return snprintf(buf, len, "\"%s\":null,", key);
  return snprintf(buf, len, "\"%s\":%.2f,", key, (double)v);
}

// snprintf returns the length it *wanted* to write; treat any overflow as failure.
bool fits(int written, size_t pos, size_t len) {
  return written >= 0 && pos + (size_t)written < len;
}

}  // namespace

size_t telemetry_to_json(const Telemetry& t, char* buf, size_t len) {
  if (buf == nullptr || len == 0) return 0;
  size_t pos = 0;
  int n;

  n = snprintf(buf + pos, len - pos, "{\"node\":\"%s\",", t.node_id ? t.node_id : "");
  if (!fits(n, pos, len)) return 0;
  pos += (size_t)n;

  if (t.ts > 0) {
    n = snprintf(buf + pos, len - pos, "\"ts\":%lld,", (long long)t.ts);
  } else {
    n = snprintf(buf + pos, len - pos, "\"ts\":null,");
  }
  if (!fits(n, pos, len)) return 0;
  pos += (size_t)n;

  n = snprintf(buf + pos, len - pos, "\"seq\":%lu,", (unsigned long)t.seq);
  if (!fits(n, pos, len)) return 0;
  pos += (size_t)n;

  n = put_float(buf + pos, len - pos, "temp_c", t.temp_c);
  if (!fits(n, pos, len)) return 0;
  pos += (size_t)n;

  n = put_float(buf + pos, len - pos, "rh_pct", t.rh_pct);
  if (!fits(n, pos, len)) return 0;
  pos += (size_t)n;

  n = snprintf(buf + pos, len - pos, "\"rssi_dbm\":%d,\"uptime_s\":%lu,\"fw\":\"%s\"}", t.rssi_dbm,
               (unsigned long)t.uptime_s, t.fw ? t.fw : "");
  if (!fits(n, pos, len)) return 0;
  pos += (size_t)n;

  return pos;
}

size_t status_to_json(const char* node_id, const char* state, int64_t ts, char* buf, size_t len) {
  if (buf == nullptr || len == 0) return 0;
  int n;
  if (ts > 0) {
    n = snprintf(buf, len, "{\"node\":\"%s\",\"state\":\"%s\",\"ts\":%lld}", node_id, state, (long long)ts);
  } else {
    n = snprintf(buf, len, "{\"node\":\"%s\",\"state\":\"%s\"}", node_id, state);
  }
  return fits(n, 0, len) ? (size_t)n : 0;
}

bool reading_is_plausible(float temp_c, float rh_pct) {
  if (isnan(temp_c) || isnan(rh_pct)) return false;
  if (temp_c < -40.0f || temp_c > 125.0f) return false;
  if (rh_pct < 0.0f || rh_pct > 100.0f) return false;
  return true;
}

size_t make_topic(char* buf, size_t len, const char* prefix, const char* node_id, const char* leaf) {
  int n = snprintf(buf, len, "%s/%s/%s", prefix, node_id, leaf);
  return fits(n, 0, len) ? (size_t)n : 0;
}

void node_id_from_mac(const uint8_t mac[6], char* out, size_t len) {
  snprintf(out, len, "esp32-%02x%02x%02x", mac[3], mac[4], mac[5]);
}
