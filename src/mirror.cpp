#include "mirror.h"
#include "mirror_shapes.h"
#include "config.h"

// ---------------- บัฟเฟอร์สะสมคำสั่ง ----------------
constexpr size_t STAGE_MAX = 8192;          // ก้อนใหญ่สุดที่ส่งต่อเป็น 1 ข้อความ WebSocket
static uint8_t  g_stage[STAGE_MAX];
static size_t   g_len = 0;
static uint32_t g_firstMs = 0;

static inline void p8(uint8_t v)   { g_stage[g_len++] = v; }
static inline void p16(uint16_t v) { g_stage[g_len++] = (uint8_t)v; g_stage[g_len++] = (uint8_t)(v >> 8); }

static void stageFlush() {
  if (g_len) { mobileSink(g_stage, g_len); g_len = 0; }
}
// เตรียมที่ว่าง n ไบต์ (ถ้าไม่พอ ส่งของเก่าออกก่อน) ; false = ใหญ่เกินบัฟเฟอร์
static bool reserve(size_t n) {
  if (n > STAGE_MAX) return false;
  if (g_len + n > STAGE_MAX) stageFlush();
  if (g_len == 0) g_firstMs = millis();
  return true;
}

void mirrorFlush(uint32_t now, bool force) {
  if (!g_len) return;
  if (!mobileActive()) { g_len = 0; return; }
  if (!force && now - g_firstMs < 16 && g_len < 3000) return;     // รวมคำสั่งทุก ~16 ms เป็นก้อนเดียว
  stageFlush();
}

// ---------------- ตัวเข้ารหัสคำสั่ง ----------------
static void recFill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;
  if (w <= 0 || h <= 0) return;
  if (!reserve(11)) return;
  p8(0x01); p16(x); p16(y); p16(w); p16(h); p16(c);
}
static inline void recHL(int x, int y, int w, uint16_t c) { recFill(x, y, w, 1, c); }
static inline void recPx(int x, int y, uint16_t c)        { recFill(x, y, 1, 1, c); }
static void recLine(int x0, int y0, int x1, int y1, uint16_t c) {
  if (!reserve(11)) return;
  p8(0x04); p16((uint16_t)x0); p16((uint16_t)y0); p16((uint16_t)x1); p16((uint16_t)y1); p16(c);
}
static void recClear(uint16_t c) { if (!reserve(3)) return; p8(0x07); p16(c); }

// pgm = ข้อมูลอยู่ใน flash ; swapped = ค่าในอาร์เรย์สลับไบต์ (SwapBytes=false) ต้องกลับก่อนส่ง
static void recImage(int x, int y, int w, int h, const uint16_t* d, bool pgm, bool swapped) {
  if (w <= 0 || h <= 0) return;
  const size_t n = (size_t)w * h;
  auto px = [&](size_t i) -> uint16_t {
    uint16_t v = pgm ? pgm_read_word(d + i) : d[i];
    return swapped ? (uint16_t)((v >> 8) | (v << 8)) : v;
  };
  if (w > 4000) return;
  const int bandRows = (int)((STAGE_MAX - 9) / ((size_t)w * 2));   // ภาพที่ดิบแล้วใหญ่กว่าบัฟเฟอร์ -> แบ่งส่งเป็นแถบแนวนอน
  if (bandRows < h) {
    for (int r = 0; r < h; r += bandRows) {
      int hh = h - r < bandRows ? h - r : bandRows;
      recImage(x, y + r, w, hh, d + (size_t)r * w, pgm, swapped);
    }
    return;
  }
  size_t runs = 0;                                        // รอบแรก: นับจำนวน run เพื่อเลือกรูปแบบที่เล็กกว่า
  for (size_t i = 0; i < n;) {
    uint16_t c = px(i); size_t k = 1;
    while (i + k < n && k < 255 && px(i + k) == c) k++;
    runs++; i += k;
  }
  const size_t sRle = 9 + runs * 3, sRaw = 9 + n * 2;
  const bool rle = sRle < sRaw;
  const size_t need = rle ? sRle : sRaw;
  if (!reserve(need)) return;                             // ใหญ่เกินบัฟเฟอร์ -> ข้าม
  p8(rle ? 0x06 : 0x05); p16((uint16_t)x); p16((uint16_t)y); p16(w); p16(h);
  if (rle) {
    for (size_t i = 0; i < n;) {
      uint16_t c = px(i); size_t k = 1;
      while (i + k < n && k < 255 && px(i + k) == c) k++;
      p8((uint8_t)k); p16(c); i += k;
    }
  } else for (size_t i = 0; i < n; i++) p16(px(i));
}

#define REC if (mobileActive())

// ---------------- MirrorTFT : เรียกของเดิมก่อน แล้วบันทึก ----------------
void MirrorTFT::setSwapBytes(bool s) { swapB = s; TFT_eSPI::setSwapBytes(s); }
void MirrorTFT::fillScreen(uint32_t c) { TFT_eSPI::fillScreen(c); REC recClear((uint16_t)c); }
void MirrorTFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  TFT_eSPI::fillRect(x, y, w, h, c); REC recFill(x, y, w, h, (uint16_t)c);
}
void MirrorTFT::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  TFT_eSPI::drawRect(x, y, w, h, c);
  REC { if (w <= 0 || h <= 0) return; recHL(x, y, w, c); recHL(x, y + h - 1, w, c); recFill(x, y + 1, 1, h - 2, c); recFill(x + w - 1, y + 1, 1, h - 2, c); }
}
void MirrorTFT::drawPixel(int32_t x, int32_t y, uint32_t c) { TFT_eSPI::drawPixel(x, y, c); REC recPx(x, y, (uint16_t)c); }
void MirrorTFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { TFT_eSPI::drawFastHLine(x, y, w, c); REC recHL(x, y, w, (uint16_t)c); }
void MirrorTFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { TFT_eSPI::drawFastVLine(x, y, h, c); REC recFill(x, y, 1, h, (uint16_t)c); }
void MirrorTFT::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
  TFT_eSPI::drawLine(x0, y0, x1, y1, c); REC recLine(x0, y0, x1, y1, (uint16_t)c);
}
void MirrorTFT::drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
  TFT_eSPI::drawCircle(x, y, r, c);
  REC shCircle(x, y, r, [&](int px, int py) { recPx(px, py, (uint16_t)c); });
}
void MirrorTFT::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
  TFT_eSPI::fillCircle(x, y, r, c);
  REC shFillCircle(x, y, r, [&](int px, int py, int w) { recHL(px, py, w, (uint16_t)c); });
}
void MirrorTFT::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
  TFT_eSPI::fillTriangle(x0, y0, x1, y1, x2, y2, c);
  REC shTriangle(x0, y0, x1, y1, x2, y2, [&](int px, int py, int w) { recHL(px, py, w, (uint16_t)c); });
}
void MirrorTFT::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  TFT_eSPI::fillRoundRect(x, y, w, h, r, c);
  REC shRoundFill(x, y, w, h, r, [&](int px, int py, int pw) { recHL(px, py, pw, (uint16_t)c); });
}
void MirrorTFT::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  TFT_eSPI::drawRoundRect(x, y, w, h, r, c);
  REC shRoundOutline(x, y, w, h, r, [&](int px, int py, int pw) { recHL(px, py, pw, (uint16_t)c); });
}
void MirrorTFT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data) {
  TFT_eSPI::pushImage(x, y, w, h, data); REC recImage(x, y, w, h, data, false, !swapB);
}
void MirrorTFT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  TFT_eSPI::pushImage(x, y, w, h, data); REC recImage(x, y, w, h, data, true, !swapB);
}
void MirrorTFT::drawBitmap(int16_t x, int16_t y, const uint8_t* bm, int16_t w, int16_t h, uint16_t c) {
  TFT_eSPI::drawBitmap(x, y, bm, w, h, c);
  REC {
    const int nb = (w + 7) / 8;
    for (int j = 0; j < h; j++) {
      int i = 0;
      while (i < w) {
        if (!(pgm_read_byte(bm + j * nb + (i >> 3)) & (0x80 >> (i & 7)))) { i++; continue; }
        int k = i; while (k < w && (pgm_read_byte(bm + j * nb + (k >> 3)) & (0x80 >> (k & 7)))) k++;
        recHL(x + i, y + j, k - i, c); i = k;
      }
    }
  }
}
