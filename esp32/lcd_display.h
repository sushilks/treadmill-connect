#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "esp_err.h"
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display states
typedef enum {
    DISPLAY_STATE_STARTUP,
    DISPLAY_STATE_BLE_READY,
    DISPLAY_STATE_BLE_CONNECTED,
    DISPLAY_STATE_WIFI_CONNECTING,
    DISPLAY_STATE_WIFI_CONNECTED,
    DISPLAY_STATE_CLOUD_CONNECTING,
    DISPLAY_STATE_CLOUD_CONNECTED,
    DISPLAY_STATE_DOWNLOADING,
    DISPLAY_STATE_FLASHING,
    DISPLAY_STATE_ERROR
} display_state_t;

/**
 * @brief Initialize the LCD display
 * 
 * @return ESP_OK on success
 */
esp_err_t lcd_display_init(void);

/**
 * @brief Show startup sequence
 */
void lcd_display_show_startup(void);

/**
 * @brief Show banner with title and message
 * 
 * @param title Title text
 * @param message Message text
 */
void lcd_display_show_banner(const char *title, const char *message);

/**X
 * @brief Show BLE ready state
 */
void lcd_display_show_ble_ready(void);

/**
 * @brief Show BLE connected state
 */
void lcd_display_show_ble_connected(void);

/**
 * @brief Show WiFi connecting state
 * 
 * @param ssid WiFi SSID
 */
void lcd_display_show_wifi_connecting(const char *ssid);

/**
 * @brief Show WiFi connected state
 * 
 * @param ip_address IP address string
 */
void lcd_display_show_wifi_connected(const char *ip_address);

/**
 * @brief Show cloud connecting state
 */
void lcd_display_show_cloud_connecting(void);

/**
 * @brief Show cloud connected state
 */
void lcd_display_show_cloud_connected(void);

/**
 * @brief Show firmware download progress
 * 
 * @param version Firmware version being downloaded
 * @param progress Progress percentage (0-100)
 */
void lcd_display_show_download_progress(const char *version, int progress);

/**
 * @brief Show firmware flashing progress
 * 
 * @param filename File being flashed
 * @param progress Progress percentage (0-100)
 */
void lcd_display_show_flash_progress(const char *filename, int progress);

/**
 * @brief Show error message
 * 
 * @param error_message Error message to display
 */
void lcd_display_show_error(const char *error_message);

/**
 * @brief Clear display
 */
void lcd_display_clear(void);

/**
 * @brief Set backlight level
 * 
 * @param level Backlight level (0-100)
 */
void lcd_display_set_backlight(uint8_t level);

/**
 * @brief LVGL display flush callback
 * 
 * This callback is used by LVGL to flush the display buffer to the LCD.
 * It must be registered with the LVGL display driver during initialization.
 * 
 * @param disp_drv Pointer to the LVGL display driver
 * @param area Area to be flushed
 * @param color_p Pointer to color buffer
 */
void lcd_display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);

#ifdef __cplusplus
}
#endif

#endif // LCD_DISPLAY_H
