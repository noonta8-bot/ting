#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "mirror.h"
#include "ecu.h"

extern MirrorTFT tft;      // TFT_eSPI + ส่งภาพหน้าจอให้แอปมือถือ (mirror.cpp)

void uiBegin();                      // init จอ + โหลดค่าที่บันทึก + splash (NUI ssk Remap)
void uiLoop(uint32_t now);           // เรียกทุก loop (คุมเฟรมเรตภายใน)
void uiTap(int x, int y);
// ปัด: dx,dy = ระยะที่ปัด, x0,y0 = จุดเริ่มแตะ (หน้า MAP ใช้ปัดเลื่อนตาราง, หน้าอื่นใช้เปลี่ยนหน้า)
void uiSwipe(int dx, int dy, int x0, int y0);
void uiForceRedraw();                // วาดหน้าปัจจุบันซ้ำทั้งหน้า (ใช้ตอนมือถือเพิ่งต่อ เพื่อให้ได้ภาพครบ)
