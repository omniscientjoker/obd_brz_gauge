/**
 * @file
 * @brief ESP LCD touch: CST816S (I2C)
 */

#pragma once
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_touch.h"

/**
 * @brief Create a new CST816S touch driver
 *
 * @note  The I2C bus must be initialized before use this function.
 *
 * @param i2c_bus I2C master bus handle
 * @param config Touch panel configuration
 * @param tp Touch panel handle
 * @return
 *      - ESP_OK: on success
 */
esp_err_t esp_lcd_touch_new_i2c_cst816(i2c_master_bus_handle_t i2c_bus, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp);

/**
 * @brief I2C address of the CST816S controller
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS    (0x15)

// I2C settings.  The Waveshare 1.85B routes CST816S onto the shared
// GPIO10/11 bus; GPIO1 is the controller reset line (not SDA).
#if CONFIG_OBD_HW_VERSION_V1_WAVESHARE
#define I2C_Touch_SDA_IO            11              /*!< CST816S SDA */
#define I2C_Touch_SCL_IO            10              /*!< CST816S SCL */
#define I2C_Touch_RST_IO            1               /*!< CST816S reset */
#else
#define I2C_Touch_SDA_IO            1               /*!< GPIO number used for I2C master data  */
#define I2C_Touch_SCL_IO            3               /*!< GPIO number used for I2C master clock */
#define I2C_Touch_RST_IO            -1              /*!< No dedicated reset line */
#endif
#define I2C_Touch_INT_IO            4               /*!< CST816S interrupt */
#define I2C_Touch_MASTER_FREQ_HZ    400000          /*!< I2C master clock frequency */

extern esp_lcd_touch_handle_t tp;

/* A missing touch controller must not prevent the display-only gauge from booting. */
esp_err_t Touch_Init(void);
