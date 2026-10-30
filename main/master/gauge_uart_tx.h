#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t gauge_uart_tx_start(void);
void gauge_uart_tx_send_config_now(void);
