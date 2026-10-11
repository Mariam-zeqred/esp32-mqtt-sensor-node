// Host unit tests for lib/node_core (no hardware, no Arduino).
//   make test-core          (plain g++)
//   pio test -e native      (PlatformIO native env)
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Backoff.h"
#include "Telemetry.h"
#include "minitest.h"

// ---------- Telemetry JSON ----------

static Telemetry sample() {
  Telemetry t;
  t.node_id = "esp32-a1b2c3";
  t.ts = 1791676800;  // 2026-10-11 00:00:00 UTC
  t.seq = 42;
  t.temp_c = 36.516f;
  t.rh_pct = 61.25f;
  t.rssi_dbm = -67;
  t.uptime_s = 1234;
  t.fw = "0.1.0";
  return t;
}

static void json_exact_document() {
  char buf[256];
  size_t n = telemetry_to_json(sample(), buf, sizeof buf);
  const char* want =
      "{\"node\":\"esp32-a1b2c3\",\"ts\":1791676800,\"seq\":42,"
      "\"temp_c\":36.52,\"rh_pct\":61.25,\"rssi_dbm\":-67,\"uptime_s\":1234,\"fw\":\"0.1.0\"}";
  CHECK_STR_EQ(buf, want);
  CHECK_EQ(n, strlen(want));
}

static void json_unsynced_clock_is_null() {
  Telemetry t = sample();
  t.ts = 0;
  char buf[256];
  CHECK(telemetry_to_json(t, buf, sizeof buf) > 0);
  CHECK(strstr(buf, "\"ts\":null,") != nullptr);
  CHECK(strstr(buf, "\"ts\":0") == nullptr);
}

static void json_nan_reading_is_null() {
  Telemetry t = sample();
  t.temp_c = NAN;
  t.rh_pct = INFINITY;
  char buf[256];
  CHECK(telemetry_to_json(t, buf, sizeof buf) > 0);
  CHECK(strstr(buf, "\"temp_c\":null,") != nullptr);
  CHECK(strstr(buf, "\"rh_pct\":null,") != nullptr);
  CHECK(strstr(buf, "nan") == nullptr);
  CHECK(strstr(buf, "inf") == nullptr);
}

static void json_too_small_buffer_returns_zero() {
  char big[256];
  size_t full = telemetry_to_json(sample(), big, sizeof big);
  // Every buffer shorter than the full document (plus NUL) must be rejected,
  // never silently truncated: a cut-off document is not valid JSON.
  for (size_t len = 1; len <= full; len++) {
    char buf[256];
    memset(buf, 'x', sizeof buf);
    CHECK_EQ(telemetry_to_json(sample(), buf, len), (size_t)0);
  }
  char exact[256];
  CHECK_EQ(telemetry_to_json(sample(), exact, full + 1), full);
  CHECK_EQ(telemetry_to_json(sample(), nullptr, 0), (size_t)0);
}

static void json_braces_balanced_and_no_trailing_comma() {
  char buf[256];
  telemetry_to_json(sample(), buf, sizeof buf);
  size_t n = strlen(buf);
  CHECK_EQ(buf[0], '{');
  CHECK_EQ(buf[n - 1], '}');
  CHECK(strstr(buf, ",}") == nullptr);
  int quotes = 0;
  for (size_t i = 0; i < n; i++) quotes += buf[i] == '"';
  CHECK_EQ(quotes % 2, 0);
}

// ---------- Status / last will ----------

static void status_documents() {
  char buf[128];
  size_t n = status_to_json("esp32-a1b2c3", "offline", 0, buf, sizeof buf);
  CHECK_STR_EQ(buf, "{\"node\":\"esp32-a1b2c3\",\"state\":\"offline\"}");
  CHECK(n > 0);
  status_to_json("esp32-a1b2c3", "online", 1791676800, buf, sizeof buf);
  CHECK_STR_EQ(buf, "{\"node\":\"esp32-a1b2c3\",\"state\":\"online\",\"ts\":1791676800}");
  CHECK_EQ(status_to_json("esp32-a1b2c3", "online", 1, buf, 10), (size_t)0);
}

// ---------- Plausibility ----------

static void plausibility_range() {
  CHECK(reading_is_plausible(36.5f, 61.0f));
  CHECK(reading_is_plausible(-40.0f, 0.0f));
  CHECK(reading_is_plausible(125.0f, 100.0f));
  CHECK(!reading_is_plausible(NAN, 50.0f));
  CHECK(!reading_is_plausible(20.0f, NAN));
  CHECK(!reading_is_plausible(-40.5f, 50.0f));
  CHECK(!reading_is_plausible(130.0f, 50.0f));
  CHECK(!reading_is_plausible(20.0f, 100.5f));
  CHECK(!reading_is_plausible(20.0f, -1.0f));
}

// ---------- Topics and node id ----------

static void topics_and_node_id() {
  char topic[64];
  CHECK_EQ(make_topic(topic, sizeof topic, "farm", "esp32-a1b2c3", "telemetry"), strlen("farm/esp32-a1b2c3/telemetry"));
  CHECK_STR_EQ(topic, "farm/esp32-a1b2c3/telemetry");
  CHECK_EQ(make_topic(topic, 8, "farm", "esp32-a1b2c3", "status"), (size_t)0);

  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xA1, 0xB2, 0xC3};
  char id[16];
  node_id_from_mac(mac, id, sizeof id);
  CHECK_STR_EQ(id, "esp32-a1b2c3");
}

// ---------- Backoff ----------

static uint32_t rng_mid() { return 20; }   // 20 % 41 == 20 -> jitter 0 for jitter_pct 20
static uint32_t rng_low() { return 0; }    // -jitter_pct
static uint32_t rng_high() { return 40; }  // +jitter_pct

static void backoff_doubles_and_caps() {
  Backoff b(1000, 60000, 20, rng_mid);
  CHECK_EQ(b.next(), 1000u);
  CHECK_EQ(b.next(), 2000u);
  CHECK_EQ(b.next(), 4000u);
  CHECK_EQ(b.next(), 8000u);
  CHECK_EQ(b.next(), 16000u);
  CHECK_EQ(b.next(), 32000u);
  CHECK_EQ(b.next(), 60000u);  // 64000 capped
  CHECK_EQ(b.next(), 60000u);
  CHECK_EQ(b.attempts(), 8u);
}

static void backoff_reset() {
  Backoff b(500, 10000, 0, nullptr);
  b.next();
  b.next();
  b.next();
  CHECK_EQ(b.attempts(), 3u);
  b.reset();
  CHECK_EQ(b.attempts(), 0u);
  CHECK_EQ(b.next(), 500u);
}

static void backoff_jitter_bounds() {
  Backoff lo(1000, 60000, 20, rng_low);
  Backoff hi(1000, 60000, 20, rng_high);
  CHECK_EQ(lo.next(), 800u);
  CHECK_EQ(hi.next(), 1200u);
  CHECK_EQ(lo.next(), 1600u);
  CHECK_EQ(hi.next(), 2400u);
}

static void backoff_never_overflows() {
  Backoff b(1000, 60000, 20, rng_high);
  for (int i = 0; i < 100; i++) {
    uint32_t d = b.next();
    CHECK(d <= 72000u);  // cap + 20 %
  }
  CHECK_EQ(b.next(), 72000u);
}

int main() {
  printf("node_core host tests\n");
  RUN(json_exact_document);
  RUN(json_unsynced_clock_is_null);
  RUN(json_nan_reading_is_null);
  RUN(json_too_small_buffer_returns_zero);
  RUN(json_braces_balanced_and_no_trailing_comma);
  RUN(status_documents);
  RUN(plausibility_range);
  RUN(topics_and_node_id);
  RUN(backoff_doubles_and_caps);
  RUN(backoff_reset);
  RUN(backoff_jitter_bounds);
  RUN(backoff_never_overflows);
  return mt_summary();
}
