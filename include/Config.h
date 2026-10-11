// Tuning knobs. Nothing secret lives here (see secrets.example.h).
#pragma once

// --- Sampling ---
#define SAMPLE_PERIOD_S 30          // seconds between readings
#define DEEP_SLEEP_ENABLED 0        // 1: sleep between readings (battery), 0: stay connected (mains)
#define SENSOR_FAIL_REBOOT_LIMIT 10 // consecutive failed reads before a restart (0 = never)

// --- I2C / SHT31 (ESP32 DevKit v1 defaults) ---
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define SHT31_ADDR 0x44             // 0x45 if the ADDR pin is pulled high

// --- MQTT ---
#define TOPIC_PREFIX "farm"         // topics: farm/<node_id>/telemetry and farm/<node_id>/status
#define MQTT_KEEPALIVE_S 30         // broker declares us dead after 1.5x this -> last will fires
#define MQTT_BUFFER_BYTES 512       // PubSubClient default (256) is too small for TLS + payload
#define MQTT_RETAIN_TELEMETRY false
#define MQTT_RETAIN_STATUS true

// --- Reconnect backoff (shared by Wi-Fi and MQTT) ---
#define BACKOFF_BASE_MS 1000
#define BACKOFF_CAP_MS 60000
#define BACKOFF_JITTER_PCT 20

// --- Time ---
#define NTP_SERVER "pool.ntp.org"
#define NTP_SYNC_TIMEOUT_MS 10000   // give up waiting, publish ts:null, keep trying next cycle
#define TIME_VALID_AFTER 1700000000 // any epoch below this (Nov 2023) means the clock is unset

// --- Misc ---
#define SERIAL_BAUD 115200
#define WIFI_CONNECT_TIMEOUT_MS 15000
