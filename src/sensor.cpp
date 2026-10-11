#include "sensor.h"

#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "Config.h"
#include "Telemetry.h"

namespace sensor {

static Adafruit_SHT31 sht31;

bool begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!sht31.begin(SHT31_ADDR)) {
    Serial.printf("[sht31] not found at 0x%02x (check SDA=%d SCL=%d, 3V3, GND)\n", SHT31_ADDR, I2C_SDA_PIN,
                  I2C_SCL_PIN);
    return false;
  }
  sht31.heater(false);  // the built-in heater is for condensation removal, never for measuring
  Serial.println("[sht31] ok");
  return true;
}

bool read(float& temp_c, float& rh_pct) {
  temp_c = NAN;
  rh_pct = NAN;
  float t, h;
  if (!sht31.readBoth(&t, &h)) {
    Serial.println("[sht31] read failed (CRC or no ACK)");
    return false;
  }
  if (!reading_is_plausible(t, h)) {
    Serial.printf("[sht31] implausible reading t=%.2f h=%.2f, dropped\n", t, h);
    return false;
  }
  temp_c = t;
  rh_pct = h;
  return true;
}

}  // namespace sensor
