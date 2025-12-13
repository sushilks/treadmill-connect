#ifndef FTMS_SERVER_H
#define FTMS_SERVER_H

#include "Bridge.h"

class FTMSServer {
public:
  void init();
  void update();

private:
  NimBLEServer *pServer;
  NimBLECharacteristic *pDataChar;
  NimBLECharacteristic *pControlChar;
  NimBLECharacteristic *pStatusChar;

  // Callbacks
  friend class ControlPointCallbacks;
};

#endif
