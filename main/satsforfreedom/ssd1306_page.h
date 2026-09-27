#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/******************************************************************************
 * @brief Use page addressing for the 2.A board's 128x32 SSD1306 display.
 *
 * @param panel Initialized SSD1306 panel handle.
 * @param io I2C panel IO handle for the same display.
 * @return ESP_OK on success, or an ESP-IDF error if the command fails.
 *
 * Replaces the panel's bitmap writer. LVGL must supply complete 128x32 frames;
 * the writer sends four 128-byte pages, matching the older working firmware.
 ******************************************************************************/
esp_err_t sff_ssd1306_enable_page_mode(esp_lcd_panel_handle_t panel,
                                       esp_lcd_panel_io_handle_t io);
