#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// ============================================================
//  Mirror : ดักทุกคำสั่งวาดจอ แล้วส่งเป็นชุดคำสั่งไบนารีไปแอปมือถือ
//  มือถือเล่นคำสั่งซ้ำลง canvas 320x240 -> ได้หน้าจอเหมือนเกจ 100% ทุกหน้า/ทุกเมนู
//
//  โปรโตคอล (ฝั่งเกจ -> มือถือ, WebSocket binary, little-endian, 1 ข้อความ = หลายคำสั่งต่อกัน)
//    0x01 FILL     x u16, y u16, w u16, h u16, color u16            ระบายสี่เหลี่ยม (ใช้แทน พิกเซล/เส้นแนวตั้ง-นอน/สี่เหลี่ยม)
//    0x04 LINE     x0 i16, y0 i16, x1 i16, y1 i16, color u16        เส้นตรง (Bresenham)
//    0x05 IMG_RAW  x i16, y i16, w u16, h u16, pixel u16 x (w*h)    ภาพ RGB565
//    0x06 IMG_RLE  x i16, y i16, w u16, h u16, { n u8, color u16 }* ภาพแบบ run-length (ข้อความ/ตัวเลขบนพื้นเรียบเล็กลงมาก)
//    0x07 CLEAR    color u16                                        ล้างทั้งจอ
//  ภาพที่ใหญ่เกินบัฟเฟอร์ (เช่นโปสเตอร์ตอนบูต) จะไม่ถูกส่ง
// ============================================================

// ---- ฝั่งผู้ให้บริการเครือข่าย (mobile.cpp) ----
bool mobileActive();                              // true = มีมือถือต่ออยู่ (ไม่มี = ไม่เสียเวลาเข้ารหัส)
bool mobileSink(const uint8_t* d, size_t n);      // ส่งข้อความหนึ่งก้อนเข้าคิว (false = คิวเต็ม)

void mirrorFlush(uint32_t now, bool force = false);   // เรียกจาก loop(): ปล่อยคำสั่งที่สะสมไว้

// ใช้แทน TFT_eSPI ในทั้งโปรเจกต์: เรียกฟังก์ชันเดิมของ TFT_eSPI ก่อน (จอจริงเหมือนเดิมทุกพิกเซล) แล้วบันทึกคำสั่งให้มือถือ
class MirrorTFT : public TFT_eSPI {
public:
  MirrorTFT() : TFT_eSPI() {}
  void setSwapBytes(bool s);
  void fillScreen(uint32_t c);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawPixel(int32_t x, int32_t y, uint32_t c);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c);
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data);
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data);
  void drawBitmap(int16_t x, int16_t y, const uint8_t* bm, int16_t w, int16_t h, uint16_t c);
private:
  bool swapB = false;
};
