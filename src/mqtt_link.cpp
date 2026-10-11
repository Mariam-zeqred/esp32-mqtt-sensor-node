#include "mqtt_link.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

#include "Config.h"
#include "Telemetry.h"
#include "net.h"
#include "secrets.h"

// Root CA embedded from certs/ca.crt by `board_build.embed_txtfiles` (NUL-terminated).
extern const uint8_t ca_crt_start[] asm("_binary_certs_ca_crt_start");

namespace mqtt {

static WiFiClientSecure tls;
static PubSubClient client(tls);
static char telemetry_topic[64];
static char status_topic[64];
static char will_payload[96];
static const char* g_node_id = "";
static uint32_t next_attempt_ms = 0;

void begin(const char* node_id) {
  g_node_id = node_id;
  make_topic(telemetry_topic, sizeof telemetry_topic, TOPIC_PREFIX, node_id, "telemetry");
  make_topic(status_topic, sizeof status_topic, TOPIC_PREFIX, node_id, "status");
  status_to_json(node_id, "offline", 0, will_payload, sizeof will_payload);
  tls.setCACert((const char*)ca_crt_start);
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setKeepAlive(MQTT_KEEPALIVE_S);
  client.setBufferSize(MQTT_BUFFER_BYTES);
}

bool connected() { return client.connected(); }
void loop() { client.loop(); }

bool ensure(Backoff& backoff, uint32_t now_ms) {
  if (client.connected()) return true;
  if ((int32_t)(now_ms - next_attempt_ms) < 0) return false;

  Serial.printf("[mqtt] connecting to %s:%d as %s\n", MQTT_HOST, MQTT_PORT, g_node_id);
  // Last will: retained "offline" on the status topic, QoS 1, delivered by the broker
  // when the keepalive lapses without a clean DISCONNECT (power cut, Wi-Fi loss, crash).
  bool ok = client.connect(g_node_id, MQTT_USER, MQTT_PASS, status_topic, 1, MQTT_RETAIN_STATUS, will_payload);
  if (ok) {
    backoff.reset();
    Serial.println("[mqtt] connected");
    publish_status("online");
    return true;
  }
  uint32_t wait = backoff.next();
  next_attempt_ms = now_ms + wait;
  // state(): -4 timeout, -2 network failed (usually TLS: wrong CA or clock not set), 5 unauthorised
  Serial.printf("[mqtt] failed, state=%d, retry #%lu in %lu ms\n", client.state(), (unsigned long)backoff.attempts(),
                (unsigned long)wait);
  return false;
}

bool publish_telemetry(const char* json) {
  bool ok = client.publish(telemetry_topic, json, MQTT_RETAIN_TELEMETRY);
  Serial.printf("[mqtt] %s %s %s\n", ok ? "sent" : "FAILED", telemetry_topic, json);
  return ok;
}

bool publish_status(const char* state) {
  char payload[96];
  if (status_to_json(g_node_id, state, net::now_unix(), payload, sizeof payload) == 0) return false;
  return client.publish(status_topic, payload, MQTT_RETAIN_STATUS);
}

void disconnect() {
  client.disconnect();
  tls.stop();
}

}  // namespace mqtt
