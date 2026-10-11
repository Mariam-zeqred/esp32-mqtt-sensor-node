// SHT31 temperature/humidity over I2C.
#pragma once

namespace sensor {

bool begin();

// Reads both values. On failure returns false and leaves NaN in the outputs.
// A read that succeeds on the bus but is outside the sensor's range also counts as a failure.
bool read(float& temp_c, float& rh_pct);

}  // namespace sensor
