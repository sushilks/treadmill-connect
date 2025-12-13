#include "Display.h"
#include "GUI_Paint.h"

Display display;
UBYTE *AppImage;

void Display::init() {
  // Vendor Driver Initialization
  Config_Init();
  LCD_Init();
  LCD_SetBacklight(1000);

  // Allocate Framebuffer
  UWORD Imagesize = LCD_WIDTH * LCD_HEIGHT * 2;
  if ((AppImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    // Serial.println("Failed to apply for black memory...");
    return;
  }

#define ORANGE 0xFD20

  // Clear buffer
  Paint_NewImage(LCD_WIDTH, LCD_HEIGHT, 0, BLACK);
  Paint_SelectImage(AppImage);
  Paint_Clear(BLACK);

  // Draw Splash
  Paint_DrawString_EN(10, 10, "Treadmill", &Font24, BLACK, ORANGE);
  Paint_DrawString_EN(10, 40, "Bridge v1.0", &Font16, BLACK, WHITE);

  LCD_Display(AppImage);
  delay(1000);
}

void Display::update() {
  // Clear buffer
  Paint_Clear(BLACK);

  if (!bridgeState.connected_to_ifit) {
    Paint_DrawString_EN(10, 10, "Scanning...", &Font24, BLACK, YELLOW);
    Paint_DrawString_EN(10, 50, "Waiting for", &Font16, BLACK, WHITE);
    Paint_DrawString_EN(10, 70, "Treadmill", &Font16, BLACK, WHITE);
  } else {
    // Connected - 2 Column Layout (240x135)
    // Header (0-25)
#define BLUE 0x001F
#define GRAY 0X8430

    // Layout Constants
    int w = LCD_WIDTH;
    int h_split = w / 2;
    int row1 = 35;
    int row2 = 70;
    int row3 = 105;

    // Header
    Paint_DrawRectangle(0, 0, h_split, 25, GREEN, DOT_PIXEL_1X1,
                        DRAW_FILL_FULL);
    Paint_DrawString_EN(h_split / 2 - 20, 5, "iFIT", &Font16, GREEN, BLACK);

    if (bridgeState.connected_to_ftms) {
      Paint_DrawRectangle(h_split, 0, w, 25, BLUE, DOT_PIXEL_1X1,
                          DRAW_FILL_FULL);
      Paint_DrawString_EN(h_split + (h_split / 2 - 20), 5, "APP", &Font16, BLUE,
                          WHITE);
    } else {
      Paint_DrawRectangle(h_split, 0, w, 25, GRAY, DOT_PIXEL_1X1,
                          DRAW_FILL_FULL);
      Paint_DrawString_EN(h_split + (h_split / 2 - 20), 5, "APP", &Font16, GRAY,
                          BLACK);
    }

    // Dynamic Columns
    // Col 1 (Left): Speed, Incline, Calories
    // Col 2 (Right): Time, Dist

    // Speed (MPH)
    Paint_DrawString_EN(10, row1, "Spd", &Font16, BLACK, WHITE);
    Paint_DrawFloatNum(60, row1, bridgeState.speed_kph * 0.621371f, 1, &Font24,
                       BLACK, WHITE);

    // Incline (%)
    Paint_DrawString_EN(10, row2, "Inc", &Font16, BLACK, WHITE);
    Paint_DrawFloatNum(60, row2, bridgeState.incline_pct, 1, &Font24, BLACK,
                       WHITE);

    // Calories
    char buf[32];
    snprintf(buf, sizeof(buf), "Cal %d", bridgeState.calories);
    Paint_DrawString_EN(10, row3, buf, &Font16, BLACK, WHITE);

    // Right Column
    int col2_x = h_split + 10;

    // Time
    uint16_t hrs = bridgeState.elapsed_time_s / 3600;
    uint16_t min = (bridgeState.elapsed_time_s % 3600) / 60;
    uint16_t sec = bridgeState.elapsed_time_s % 60;
    if (hrs > 0)
      snprintf(buf, sizeof(buf), "%d:%02d:%02d", hrs, min, sec);
    else
      snprintf(buf, sizeof(buf), "%02d:%02d", min, sec);
    Paint_DrawString_EN(col2_x, row1, buf, &Font24, BLACK, WHITE);

    // Distance (Miles)
    snprintf(buf, sizeof(buf), "%.2f mi", bridgeState.distance_m * 0.000621371);
    Paint_DrawString_EN(col2_x, row2, buf, &Font16, BLACK, WHITE);
  }

  // Flush to screen
  LCD_Display(AppImage);
}
