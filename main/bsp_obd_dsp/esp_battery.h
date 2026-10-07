#pragma once

#include <stdint.h>

// Start the BQ27220 polling task on the board's shared I2C bus.
void esp_battery_start(void);

// Return the latest ESP battery reading. Values are -1 until a valid reading exists.
void esp_battery_get_snapshot(int32_t *voltage_mv, int *percent);
