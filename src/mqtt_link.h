// TLS MQTT link: connect with a last will, publish telemetry and status, backoff on failure.
#pragma once
#include <stdint.h>

#include "Backoff.h"

namespace mqtt {

void begin(const char* node_id);

// Non-blocking: if disconnected, attempts to connect when the backoff says so.
// Publishes the retained "online" status after each successful connect.
bool ensure(Backoff& backoff, uint32_t now_ms);

bool connected();
void loop();  // keepalive and inbound processing; call every iteration

bool publish_telemetry(const char* json);
bool publish_status(const char* state);  // retained

// Graceful disconnect: the broker will NOT send the last will. Used before deep sleep.
void disconnect();

}  // namespace mqtt
