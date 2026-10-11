// Copy to include/secrets.h and fill in. secrets.h is git-ignored.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-pass"

#define MQTT_HOST "broker.local"    // must match the CN/SAN in certs/server.crt
#define MQTT_PORT 8883
#define MQTT_USER "node"
#define MQTT_PASS "change-me"       // the value you gave tools/broker_setup.sh
