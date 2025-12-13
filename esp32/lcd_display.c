#include "lcd_display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "LCD_DISPLAY";

// LCD Configuration (matching ESP32-S3-GEEK and Arduino)
#define LCD_HOST          SPI2_HOST
#define LCD_PIXEL_CLOCK   40000000  // 40MHz (SPI_CLOCK_DIV2 from 80MHz)
#define LCD_BK_LIGHT_GPIO 7
#define LCD_RST_GPIO      9
#define LCD_DC_GPIO       8
#define LCD_CS_GPIO       10
#define LCD_MOSI_GPIO     11
#define LCD_SCK_GPIO      12

// LCD Resolution - LANDSCAPE mode (physically oriented horizontally)
#define LCD_WIDTH         240
#define LCD_HEIGHT        135

// LEDC Configuration for backlight
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL      LEDC_CHANNEL_0
#define LEDC_DUTY_RES     LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY    5000

// Colors (RGB565 - matching Arduino)
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_RED         0xF800
#define COLOR_CYAN        0x7FFF
#define COLOR_ORANGE      0xFD20

// Static variables
static spi_device_handle_t spi_handle = NULL;
static bool lcd_initialized = false;

// Forward declarations
static void lcd_write_byte(uint8_t data, bool is_data);
static void lcd_write_word(uint16_t data);
static void lcd_write_cmd(uint8_t cmd);
static void lcd_set_cursor(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/**
 * @brief SPI pre-transfer callback to set DC pin
 */
static void spi_pre_transfer_callback(spi_transaction_t *t)
{
    gpio_set_level(LCD_DC_GPIO, (int)t->user);
}

/**
 * @brief Initialize backlight PWM
 */
static esp_err_t init_backlight(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LCD_BK_LIGHT_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    return ESP_OK;
}

/**
 * @brief Write a single byte to LCD (command or data)
 */
static void lcd_write_byte(uint8_t data, bool is_data)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
        .user = (void*)(is_data ? 1 : 0),
    };
    spi_device_transmit(spi_handle, &t);
}

/**
 * @brief Write command to LCD
 */
static void lcd_write_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_DC_GPIO, 0);
    lcd_write_byte(cmd, false);
}

/**
 * @brief Write 16-bit word as two bytes
 */
static void lcd_write_word(uint16_t data)
{
    gpio_set_level(LCD_DC_GPIO, 1);
    lcd_write_byte((data >> 8) & 0xFF, true);
    lcd_write_byte(data & 0xFF, true);
}

/**
 * @brief Set LCD cursor/window (adjusted for landscape)
 */
static void lcd_set_cursor(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_write_cmd(0x2A);  // Column address set
    lcd_write_word(x1 + 40);  // Swapped offset for landscape
    lcd_write_word(x2 + 40);
    
    lcd_write_cmd(0x2B);  // Row address set
    lcd_write_word(y1 + 52);  // Swapped offset for landscape
    lcd_write_word(y2 + 52);
    
    lcd_write_cmd(0x2C);  // Memory write
}

/**
 * @brief Initialize LCD display (matching Arduino LCD_Init)
 */
esp_err_t lcd_display_init(void)
{
    if (lcd_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing LCD display");

    // Initialize backlight
    ESP_ERROR_CHECK(init_backlight());
    lcd_display_set_backlight(140); // Match Arduino: analogWrite(DEV_BL_PIN,140)

    // Configure GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_RST_GPIO) | (1ULL << LCD_DC_GPIO) | (1ULL << LCD_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // SPI bus configuration - Match Arduino: MODE3, MSBFIRST, DIV2
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_SCK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // SPI device configuration - MODE3
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_PIXEL_CLOCK,
        .mode = 3,  // SPI_MODE3
        .spics_io_num = LCD_CS_GPIO,
        .queue_size = 7,
        .pre_cb = spi_pre_transfer_callback,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &devcfg, &spi_handle));

    // Hardware reset
    gpio_set_level(LCD_CS_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Initialize ST7789 (landscape orientation)
    // Testing MADCTL values for proper landscape orientation:
    // 0x00: No rotation
    // 0x60: 90° rotation  (MX=1, MV=1)
    // 0xA0: 180° rotation (MY=1, MX=1)
    // 0xC0: 270° rotation (MY=1, MX=1, MV=1)
    lcd_write_cmd(0x36);
    lcd_write_byte(0xA0, true);  // Try 180° rotation for landscape

    lcd_write_cmd(0x3A);
    lcd_write_byte(0x05, true);

    lcd_write_cmd(0xB2);
    lcd_write_byte(0x0C, true);
    lcd_write_byte(0x0C, true);
    lcd_write_byte(0x00, true);
    lcd_write_byte(0x33, true);
    lcd_write_byte(0x33, true);

    lcd_write_cmd(0xB7);
    lcd_write_byte(0x35, true);

    lcd_write_cmd(0xBB);
    lcd_write_byte(0x19, true);

    lcd_write_cmd(0xC0);
    lcd_write_byte(0x2C, true);

    lcd_write_cmd(0xC2);
    lcd_write_byte(0x01, true);

    lcd_write_cmd(0xC3);
    lcd_write_byte(0x12, true);

    lcd_write_cmd(0xC4);
    lcd_write_byte(0x20, true);

    lcd_write_cmd(0xC6);
    lcd_write_byte(0x0F, true);

    lcd_write_cmd(0xD0);
    lcd_write_byte(0xA4, true);
    lcd_write_byte(0xA1, true);

    lcd_write_cmd(0xE0);
    lcd_write_byte(0xD0, true);
    lcd_write_byte(0x04, true);
    lcd_write_byte(0x0D, true);
    lcd_write_byte(0x11, true);
    lcd_write_byte(0x13, true);
    lcd_write_byte(0x2B, true);
    lcd_write_byte(0x3F, true);
    lcd_write_byte(0x54, true);
    lcd_write_byte(0x4C, true);
    lcd_write_byte(0x18, true);
    lcd_write_byte(0x0D, true);
    lcd_write_byte(0x0B, true);
    lcd_write_byte(0x1F, true);
    lcd_write_byte(0x23, true);

    lcd_write_cmd(0xE1);
    lcd_write_byte(0xD0, true);
    lcd_write_byte(0x04, true);
    lcd_write_byte(0x0C, true);
    lcd_write_byte(0x11, true);
    lcd_write_byte(0x13, true);
    lcd_write_byte(0x2C, true);
    lcd_write_byte(0x3F, true);
    lcd_write_byte(0x44, true);
    lcd_write_byte(0x51, true);
    lcd_write_byte(0x2F, true);
    lcd_write_byte(0x1F, true);
    lcd_write_byte(0x1F, true);
    lcd_write_byte(0x20, true);
    lcd_write_byte(0x23, true);

    lcd_write_cmd(0x21);  // Inversion ON
    lcd_write_cmd(0x11);  // Sleep out
    lcd_write_cmd(0x29);  // Display on

    lcd_initialized = true;
    ESP_LOGI(TAG, "LCD initialized successfully");

    return ESP_OK;
}

/**
 * @brief Set backlight level
 */
void lcd_display_set_backlight(uint8_t level)
{
    if (level > 100) level = 100;
    uint32_t duty = (level * 255) / 100;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/**
 * @brief Clear display (optimized for speed)
 */
void lcd_display_clear(void)
{
    if (!lcd_initialized) return;
    
    lcd_set_cursor(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    
    // Optimized: use DMA transfer for entire screen
    uint16_t color = COLOR_BLACK;
    uint8_t color_bytes[2] = {(uint8_t)(color >> 8), (uint8_t)(color & 0xFF)};
    
    int total_pixels = LCD_WIDTH * LCD_HEIGHT;
    
    // Allocate buffer (use chunks to avoid large malloc)
    int buffer_pixels = 2046;  // Max SPI transfer / 2 bytes
    uint8_t *buffer = malloc(buffer_pixels * 2);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate clear buffer");
        return;
    }
    
    // Fill buffer with color
    for (int i = 0; i < buffer_pixels; i++) {
        buffer[i * 2] = color_bytes[0];
        buffer[i * 2 + 1] = color_bytes[1];
    }
    
    // Send buffer repeatedly to fill screen
    int pixels_sent = 0;
    while (pixels_sent < total_pixels) {
        int pixels_this_chunk = (total_pixels - pixels_sent > buffer_pixels) 
                                ? buffer_pixels 
                                : (total_pixels - pixels_sent);
        
        spi_transaction_t t = {
            .length = pixels_this_chunk * 16,
            .tx_buffer = buffer,
            .user = (void*)1,  // Data mode
        };
        spi_device_transmit(spi_handle, &t);
        
        pixels_sent += pixels_this_chunk;
    }
    
    free(buffer);
}

/**
 * @brief Fill rectangle with color (optimized version)
 */
static void fill_rect(int x, int y, int width, int height, uint16_t color)
{
    if (!lcd_initialized || width <= 0 || height <= 0) return;

    lcd_set_cursor(x, y, x + width - 1, y + height - 1);
    
    // Optimized: batch pixels instead of one-by-one
    uint8_t color_bytes[2] = {(uint8_t)(color >> 8), (uint8_t)(color & 0xFF)};
    int total_pixels = width * height;
    
    // Allocate buffer (max 2046 pixels = 4092 bytes to stay under SPI limit)
    int buffer_pixels = (total_pixels > 2046) ? 2046 : total_pixels;
    uint8_t *buffer = malloc(buffer_pixels * 2);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate fill buffer");
        return;
    }
    
    // Fill buffer with color
    for (int i = 0; i < buffer_pixels; i++) {
        buffer[i * 2] = color_bytes[0];
        buffer[i * 2 + 1] = color_bytes[1];
    }
    
    // Send buffer in chunks
    int pixels_sent = 0;
    while (pixels_sent < total_pixels) {
        int pixels_this_chunk = (total_pixels - pixels_sent > buffer_pixels) 
                                ? buffer_pixels 
                                : (total_pixels - pixels_sent);
        
        spi_transaction_t t = {
            .length = pixels_this_chunk * 16,
            .tx_buffer = buffer,
            .user = (void*)1,  // Data mode
        };
        spi_device_transmit(spi_handle, &t);
        
        pixels_sent += pixels_this_chunk;
    }
    
    free(buffer);
}

/**
 * @brief Show startup sequence
 */
void lcd_display_show_startup(void)
{
    if (!lcd_initialized) return;

    // Show "Starting" message - green full bar
    lcd_display_clear();
    fill_rect(40, 50, 160, 35, COLOR_GREEN);  // Large centered green bar
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Show "Welcome to EdgeKVM" banner
    lcd_display_show_banner("Welcome to", "EdgeKVM");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Show ready state
    lcd_display_show_ble_ready();
}

/**
 * @brief Show banner with title and message
 */
void lcd_display_show_banner(const char *title, const char *message)
{
    if (!lcd_initialized) return;

    lcd_display_clear();
    
    // Draw a simple centered box instead of random lines
    // Outer border
    fill_rect(10, 10, 220, 115, COLOR_WHITE);
    // Inner black area
    fill_rect(12, 12, 216, 111, COLOR_BLACK);
    
    // Draw title area with distinct color
    fill_rect(20, 30, 200, 30, COLOR_CYAN);
    
    // Draw message area with different color
    fill_rect(20, 75, 200, 30, COLOR_GREEN);

    ESP_LOGI(TAG, "Banner: %s - %s", title, message);
}

/**
 * @brief Show BLE ready state
 */
void lcd_display_show_ble_ready(void)
{
    lcd_display_show_banner("Ready", "BLE Available");
}

/**
 * @brief Show BLE connected state
 */
void lcd_display_show_ble_connected(void)
{
    lcd_display_show_banner("BLE", "Connected");
}

/**
 * @brief Show WiFi connecting state
 */
void lcd_display_show_wifi_connecting(const char *ssid)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "Connecting...");
    lcd_display_show_banner("Wi-Fi", msg);
}

/**
 * @brief Show WiFi connected state
 */
void lcd_display_show_wifi_connected(const char *ip_address)
{
    lcd_display_show_banner("Wi-Fi OK", ip_address);
}

/**
 * @brief Show cloud connecting state
 */
void lcd_display_show_cloud_connecting(void)
{
    lcd_display_show_banner("Cloud", "Connecting...");
}

/**
 * @brief Show cloud connected state
 */
void lcd_display_show_cloud_connected(void)
{
    lcd_display_show_banner("Cloud OK", "Connected");
}

/**
 * @brief Show firmware download progress
 */
void lcd_display_show_download_progress(const char *version, int progress)
{
    if (!lcd_initialized) return;

    char title[32];
    char msg[32];
    
    snprintf(title, sizeof(title), "Downloading");
    snprintf(msg, sizeof(msg), "%s (%d%%)", version, progress);
    
    lcd_display_clear();
    
    // Draw border
    fill_rect(10, 10, 220, 115, COLOR_BLUE);
    fill_rect(12, 12, 216, 111, COLOR_BLACK);
    
    // Draw title indicator bar
    fill_rect(20, 25, 200, 25, COLOR_BLUE);
    
    // Draw progress bar border
    fill_rect(15, 65, 210, 30, COLOR_WHITE);
    fill_rect(17, 67, 206, 26, COLOR_BLACK);
    
    // Draw progress bar fill
    int bar_width = (progress * 200) / 100;
    if (bar_width > 0) {
        fill_rect(20, 70, bar_width, 20, COLOR_GREEN);
    }
    
    ESP_LOGI(TAG, "Download: %s", msg);
}

/**
 * @brief Show firmware flashing progress
 */
void lcd_display_show_flash_progress(const char *filename, int progress)
{
    if (!lcd_initialized) return;

    char title[32];
    char msg[32];
    
    snprintf(title, sizeof(title), "Flashing");
    snprintf(msg, sizeof(msg), "%d%%", progress);
    
    lcd_display_clear();
    
    // Draw border
    fill_rect(10, 10, 220, 115, COLOR_ORANGE);
    fill_rect(12, 12, 216, 111, COLOR_BLACK);
    
    // Draw title indicator bar
    fill_rect(20, 25, 200, 25, COLOR_ORANGE);
    
    // Draw progress bar border
    fill_rect(15, 65, 210, 30, COLOR_WHITE);
    fill_rect(17, 67, 206, 26, COLOR_BLACK);
    
    // Draw progress bar fill
    int bar_width = (progress * 200) / 100;
    if (bar_width > 0) {
        fill_rect(20, 70, bar_width, 20, COLOR_ORANGE);
    }
    
    ESP_LOGI(TAG, "Flashing %s: %d%%", filename, progress);
}

/**
 * @brief Show error message
 */
void lcd_display_show_error(const char *error_message)
{
    if (!lcd_initialized) return;

    lcd_display_clear();
    
    // Draw ERROR in red
    fill_rect(10, 30, 220, 30, COLOR_RED);
    
    // Draw error message
    fill_rect(10, 70, 220, 40, COLOR_YELLOW);
    
    ESP_LOGE(TAG, "Error: %s", error_message);
}

/**
 * @brief LVGL display flush callback
 * 
 * This function is called by LVGL to flush the display buffer to the LCD.
 * It receives a rectangular area and a color buffer and must transfer it to the display.
 */
void lcd_display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (!lcd_initialized) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    
    uint16_t width = area->x2 - area->x1 + 1;
    uint16_t height = area->y2 - area->y1 + 1;
    uint32_t size = width * height;
    
    static int flush_count = 0;
    if (flush_count < 10) {
        // Sample first pixel color
        uint16_t first_pixel = ((uint16_t*)color_p)[0];
        ESP_LOGI(TAG, "Flush #%d: area(%d,%d)->(%d,%d) size=%dx%d=%d px, first_pixel=0x%04x", 
                 flush_count++, area->x1, area->y1, area->x2, area->y2, width, height, size, first_pixel);
    }
    
    // Set the drawing window
    lcd_set_cursor(area->x1, area->y1, area->x2, area->y2);
    
    // Convert LVGL color format to RGB565 and send via SPI
    // LVGL uses lv_color_t which is already RGB565 on this platform
    uint16_t *color_buf = (uint16_t *)color_p;
    
    // Send color data via SPI in chunks to avoid large single transfers
    const uint32_t chunk_size = 2046;  // Max pixels per SPI transaction
    uint32_t pixels_sent = 0;
    
    // Static buffer for byte conversion (max chunk size)
    static uint8_t byte_buffer[2046 * 2];
    
    // Set DC pin to data mode for entire transfer
    gpio_set_level(LCD_DC_GPIO, 1);
    
    while (pixels_sent < size) {
        uint32_t pixels_this_chunk = (size - pixels_sent > chunk_size) 
                                     ? chunk_size 
                                     : (size - pixels_sent);
        
        // ST7789 expects RGB565 in big-endian (MSB first)
        // Convert from little-endian to big-endian
        for (uint32_t i = 0; i < pixels_this_chunk; i++) {
            uint16_t pixel = color_buf[pixels_sent + i];
            byte_buffer[i * 2] = (pixel >> 8) & 0xFF;      // MSB first
            byte_buffer[i * 2 + 1] = pixel & 0xFF;         // LSB second
        }
        
        spi_transaction_t t = {
            .length = pixels_this_chunk * 16,  // bits
            .tx_buffer = byte_buffer,
            .user = (void*)1,  // Data mode (for pre-callback)
        };
        spi_device_transmit(spi_handle, &t);
        
        pixels_sent += pixels_this_chunk;
    }
    
    // Signal LVGL that flushing is complete
    lv_disp_flush_ready(disp_drv);
}
