#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"
#include "ecu.h"
#include "ui.h"
#include "mobile.h"

// ทัชของ CYD ใช้ SPI แยก (VSPI) จากจอ (HSPI)
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);

static bool tDown = false;
static int tx0, ty0, tx1, ty1;
static uint32_t tLastSeen = 0, tStart = 0;

static bool readTouch(int& x, int& y) {
  if (!ts.tirqTouched() || !ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 250) return false;
  x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_W - 1);
  y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_H - 1);
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
#if SCREEN_ROTATION == 3
  x = SCREEN_W - 1 - x;
  y = SCREEN_H - 1 - y;
#endif
  return true;
}

static void handleTouch(uint32_t now) {
  int x, y;
  if (readTouch(x, y)) {
    if (!tDown) { tDown = true; tx0 = x; ty0 = y; tStart = now; }
    tx1 = x; ty1 = y; tLastSeen = now;
  } else if (tDown && now - tLastSeen > 60) {      // ปล่อยนิ้ว
    tDown = false;
    int dx = tx1 - tx0, dy = ty1 - ty0;
    if ((abs(dx) > 60 && abs(dy) < 50) || (abs(dy) > 40 && abs(dx) < 45)) uiSwipe(dx, dy, tx0, ty0);
    else if (abs(dx) < 25 && abs(dy) < 25) uiTap(tx0, ty0);
  }
}

void setup() {
  Serial.begin(115200);
  touchSPI.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  ecuBegin();     // task อ่าน ECU บน core 0
  uiBegin();      // จอ + splash + หน้าแรก (core 1)
  mobileBegin();  // Wi-Fi + เว็บ/WebSocket สำหรับแอปมือถือ (task แยก core 0)
}

void loop() {
  uint32_t now = millis();
  handleTouch(now);
  uiLoop(now);
  mobileLoop(now);
  delay(1);
}
