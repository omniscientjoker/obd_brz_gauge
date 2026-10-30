#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t gauge_uart_rx_start(void);
uint8_t gauge_slave_dashboard_take(bool *changed);
bool gauge_slave_link_is_connected(void);
