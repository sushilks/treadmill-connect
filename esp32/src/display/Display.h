#ifndef DISPLAY_H
#define DISPLAY_H

#include "Bridge.h"
#include "GUI_Paint.h"
#include "LCD_Driver.h"
#include <Arduino.h>

// Forward declare if needed, or just use globals
// The vendor driver seems to be singleton/static based in C

class Display {
public:
  void init();
  void update();

private:
  // No TFT_eSPI object needed
  // We might need a frame buffer for Paint if we want to use it efficiently
  // The S3 Geek has PSRAM, so we can allocate a full buffer?
  // Let's stick to direct drawing or small buffers for now to match their
  // example.

  UWORD *_blackImage = nullptr; // Pointer for image buffer if used

  // State Tracking for redraw
  bool _was_connected = false;
  float _last_speed = -1.0;
  float _last_incline = -1.0;
  uint32_t _last_update = 0;
};

extern Display display;

#endif
