#include "ssd1306_page.h"

#include <stdint.h>

#include "esp_check.h"
#include "esp_lcd_panel_interface.h"

#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 32
#define SSD1306_PAGE_HEIGHT 8
#define SSD1306_CMD_SET_MEMORY_MODE 0x20
#define SSD1306_MEMORY_MODE_PAGE 0x02
#define SSD1306_CMD_SET_PAGE 0xB0
#define SSD1306_CMD_SET_COLUMN_LOW 0x00
#define SSD1306_CMD_SET_COLUMN_HIGH 0x10

static const char *TAG = "sff_ssd1306";
static esp_lcd_panel_handle_t page_panel;
static esp_lcd_panel_io_handle_t page_io;

/******************************************************************************
 * @brief Send one complete monochrome frame as four SSD1306 pages.
 *
 * @param panel Panel registered for page addressing.
 * @param x_start,y_start Inclusive origin of the frame.
 * @param x_end,y_end Exclusive end of the frame.
 * @param color_data Page-packed frame from the LVGL monochrome port.
 * @return ESP_OK after all pages are sent, or an ESP-IDF transfer error.
 *
 * The 2.A OLED uses a 128x32 full-frame buffer. Page addressing avoids the
 * horizontal-addressing command sequence used by the standard panel writer.
 ******************************************************************************/
static esp_err_t sff_ssd1306_draw_bitmap(esp_lcd_panel_t *panel,
                                        int x_start, int y_start, int x_end, int y_end,
                                        const void *color_data)
{
    ESP_RETURN_ON_FALSE(panel == page_panel && page_io && color_data,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid SSD1306 panel or frame");
    ESP_RETURN_ON_FALSE(x_start == 0 && y_start == 0 &&
                        x_end == SSD1306_WIDTH && y_end == SSD1306_HEIGHT,
                        ESP_ERR_INVALID_ARG, TAG, "SSD1306 page writer requires a 128x32 frame");

    const uint8_t *frame = color_data;
    for (uint8_t page = 0; page < SSD1306_HEIGHT / SSD1306_PAGE_HEIGHT; page++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(page_io, SSD1306_CMD_SET_PAGE | page, NULL, 0),
                            TAG, "Failed to select SSD1306 page");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(page_io, SSD1306_CMD_SET_COLUMN_LOW, NULL, 0),
                            TAG, "Failed to set SSD1306 low column");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(page_io, SSD1306_CMD_SET_COLUMN_HIGH, NULL, 0),
                            TAG, "Failed to set SSD1306 high column");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(page_io, -1, frame + page * SSD1306_WIDTH,
                                                       SSD1306_WIDTH),
                            TAG, "Failed to send SSD1306 page");
    }

    return ESP_OK;
}

esp_err_t sff_ssd1306_enable_page_mode(esp_lcd_panel_handle_t panel,
                                       esp_lcd_panel_io_handle_t io)
{
    ESP_RETURN_ON_FALSE(panel && io, ESP_ERR_INVALID_ARG, TAG, "Invalid SSD1306 handles");

    const uint8_t page_mode = SSD1306_MEMORY_MODE_PAGE;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, SSD1306_CMD_SET_MEMORY_MODE,
                                                  &page_mode, sizeof(page_mode)),
                        TAG, "Failed to select SSD1306 page addressing");

    page_panel = panel;
    page_io = io;
    panel->draw_bitmap = sff_ssd1306_draw_bitmap;
    return ESP_OK;
}
