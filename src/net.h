// Wi-Fi connection and NTP time, with backoff shared with the MQTT link.
#pragma once
#include <stdint.h>

#include "Backoff.h"

namespace net {

// Blocks until connected or WIFI_CONNECT_TIMEOUT_MS passes. Returns true on success.
bool wifi_connect();

// Non-blocking: if Wi-Fi is down, reconnects when the backoff says so. Returns connected state.
bool wifi_ensure(Backoff& backoff, uint32_t now_ms);

// Starts SNTP once; returns true if the system clock has been set.
bool time_sync(uint32_t timeout_ms);
bool time_is_valid();

// Unix seconds, or 0 when the clock has not been set yet.
int64_t now_unix();

}  // namespace net
