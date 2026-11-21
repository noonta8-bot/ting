#pragma once
// ตัวสร้างรูปทรง -> เส้นแนวนอน/พิกเซล (เทมเพลต ไม่พึ่ง Arduino)
// ใช้ร่วมกันระหว่าง mirror.cpp (ส่งภาพไปมือถือ) และตัวจำลองจอบนคอมพิวเตอร์ (ใช้ทดสอบ)
// อัลกอริทึมเดียวกับ TFT_eSPI / Adafruit-GFX (สามเหลี่ยมแบบ scanline, วงกลมแบบ midpoint)
#include <stdint.h>
#include <math.h>

// สามเหลี่ยมทึบ : hl(x, y, w) = เส้นแนวนอนยาว w เริ่มที่ x
template <class F> inline void shTriangle(int x0, int y0, int x1, int y1, int x2, int y2, F hl) {
  int a, b, y, last;
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y0 == y2) {
    a = b = x0;
    if (x1 < a) a = x1; else if (x1 > b) b = x1;
    if (x2 < a) a = x2; else if (x2 > b) b = x2;
    hl(a, y0, b - a + 1);
    return;
  }
  int dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
  int32_t sa = 0, sb = 0;
  last = (y1 == y2) ? y1 : y1 - 1;
  for (y = y0; y <= last; y++) {
    a = x0 + sa / dy01; b = x0 + sb / dy02; sa += dx01; sb += dx02;
    if (a > b) { int t = a; a = b; b = t; }
    hl(a, y, b - a + 1);
  }
  sa = (int32_t)dx12 * (y - y1); sb = (int32_t)dx02 * (y - y0);
  for (; y <= y2; y++) {
    a = x1 + sa / dy12; b = x0 + sb / dy02; sa += dx12; sb += dx02;
    if (a > b) { int t = a; a = b; b = t; }
    hl(a, y, b - a + 1);
  }
}
// วงกลมเส้นเดียว : px(x, y)
template <class F> inline void shCircle(int x0, int y0, int r, F px) {
  int x = 0, dx = 1, dy = r + r, p = -(r >> 1);
  px(x0 + r, y0); px(x0 - r, y0); px(x0, y0 - r); px(x0, y0 + r);
  while (x < r) {
    if (p >= 0) { dy -= 2; p -= dy; r--; }
    dx += 2; p += dx; x++;
    px(x0 + x, y0 + r); px(x0 - x, y0 + r); px(x0 - x, y0 - r); px(x0 + x, y0 - r);
    px(x0 + r, y0 + x); px(x0 - r, y0 + x); px(x0 - r, y0 - x); px(x0 + r, y0 - x);
  }
}
// วงกลมทึบ : hl(x, y, w)
template <class F> inline void shFillCircle(int x0, int y0, int r, F hl) {
  int x = 0, dx = 1, dy = r + r, p = -(r >> 1);
  hl(x0 - r, y0, dy + 1);
  while (x < r) {
    if (p >= 0) { dy -= 2; p -= dy; r--; }
    dx += 2; p += dx; x++;
    hl(x0 - r, y0 + x, 2 * r + 1); hl(x0 - r, y0 - x, 2 * r + 1);
    hl(x0 - x, y0 + r, 2 * x + 1); hl(x0 - x, y0 - r, 2 * x + 1);
  }
}
// สี่เหลี่ยมมุมมน: ระยะหุบเข้าของแถวที่ j (0..h-1)
inline int shRoundInset(int j, int h, int r) {
  if (r <= 0) return 0;
  int d = j < r ? r - j : (j >= h - r ? j - (h - r) + 1 : 0);
  if (d <= 0) return 0;
  float dy = d - 0.5f, c = sqrtf((float)r * r - dy * dy);
  return r - (int)(c + 0.5f);
}
template <class F> inline void shRoundFill(int x, int y, int w, int h, int r, F hl) {
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
  for (int j = 0; j < h; j++) { int a = shRoundInset(j, h, r); hl(x + a, y + j, w - 2 * a); }
}
template <class F> inline void shRoundOutline(int x, int y, int w, int h, int r, F hl) {
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
  for (int j = 0; j < h; j++) {
    int a = shRoundInset(j, h, r);
    if (j == 0 || j == h - 1) { hl(x + a, y + j, w - 2 * a); continue; }
    int pv = j < h / 2 ? shRoundInset(j - 1, h, r) : shRoundInset(j + 1, h, r);
    if (pv > a) { hl(x + a, y + j, pv - a); hl(x + w - pv, y + j, pv - a); }
    else        { hl(x + a, y + j, 1);      hl(x + w - 1 - a, y + j, 1); }
  }
}
// เส้นตรง (Bresenham แบบเดียวกับ TFT_eSPI::drawLine) : px(x, y)
template <class F> inline void shLine(int x0, int y0, int x1, int y1, F px) {
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) { int t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
  int dx = x1 - x0, dy = abs(y1 - y0), err = dx >> 1, ystep = y0 < y1 ? 1 : -1;
  for (; x0 <= x1; x0++) {
    if (steep) px(y0, x0); else px(x0, y0);
    err -= dy;
    if (err < 0) { y0 += ystep; err += dx; }
  }
}
