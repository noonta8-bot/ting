#pragma once
#include <Arduino.h>

// ============================================================
//  PGM-FI Smart Gauge - การตั้งค่าทั้งหมดอยู่ที่ไฟล์นี้
// ============================================================

// ---------- จอ / ทัช (ESP32-2432S028R) ----------
#define SCREEN_W        320
#define SCREEN_H        240
#define SCREEN_ROTATION 1        // 1 = ปกติ, 3 = กลับหัว 180° (ทัชจะกลับให้อัตโนมัติ)
#define PIN_BL          21       // ไฟพื้นหลังจอ (PWM)
#define SCREEN_INVERT   0        // ส่งคำสั่งกลับขั้วสีให้จอ: 0 = INVOFF (พื้นดำ สีปกติ)  1 = INVON   ถ้าพื้นขาว/สีตรงข้ามให้สลับค่านี้

#define PIN_TOUCH_CLK   25
#define PIN_TOUCH_MOSI  32
#define PIN_TOUCH_MISO  39
#define PIN_TOUCH_CS    33
#define PIN_TOUCH_IRQ   36
// ค่า calibrate ทัช (ถ้าจิ้มแล้วตำแหน่งเพี้ยน ปรับตรงนี้)
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// ---------- K-Line (ต่อผ่านวงจร L9637D / MC33290 ฯลฯ) ----------
#define K_BAUD          10400
#define PIN_K_RX        22       // CN1 pin IO22  (ต้องลดแรงดันเหลือ 3.3V !)
#define PIN_K_TX        27       // CN1 pin IO27
#define K_HAS_ECHO      1        // 1 = วงจร K-Line ส่งข้อมูลย้อนกลับมาที่ RX (ตรงกับสเปคที่ให้มา)
#define K_STRICT_CHECKSUM 0      // 1 = ทิ้งแพ็กเก็ตตอบกลับที่ checksum ไม่ผ่าน
#define K_REPLY_TIMEOUT_MS 250

// ---------- ค่าสเกลเกจ / โซนสี ----------
constexpr float RPM_MAX        = 12000.0f;  // เต็มสเกลวงล้อ
constexpr float RPM_WARN       = 8500.0f;   // เริ่มสีส้ม
constexpr float RPM_REDLINE    = 9500.0f;   // เรดไลน์ (แถบไฟเปลี่ยนเกียร์กระพริบ)
constexpr float RPM_SHIFT_START= 6000.0f;   // แถบ shift-light เริ่มติดที่รอบนี้

constexpr float EOT_COLD       = 60.0f;     // < นี้ = น้ำเงิน/ฟ้า (เครื่องยังเย็น)
constexpr float EOT_HOT        = 110.0f;    // > นี้ = เหลือง
constexpr float EOT_CRIT       = 125.0f;    // > นี้ = แดง

constexpr float BATT_LOW       = 11.8f;
constexpr float BATT_WARN_LOW  = 12.4f;
constexpr float BATT_HIGH      = 15.0f;

// ---------- UI ----------
#define FRAME_MS        28       // ~35 fps
#define HIST_MS         50       // กราฟ: เก็บตัวอย่างทุก 50 ms (300 จุด = 15 วินาที)

// ---------- NUI ssk Remap : AFR map recorder ----------
#define MAP_FILE        "/afrmap.bin"   // ไฟล์บันทึกใน flash (LittleFS)
#define TPS_TILE_USE_PCT 1              // 1 = ช่อง TPS ในหน้าใหม่แสดงค่า ECU 0-72.5 (ตรงกับแกนตาราง MAP)
                                        // 0 = แสดงสเกล 0-100%

// ---------- หน้า HOME ----------
#define HOME_USE_BG     1        // 1 = พื้นหลังสำเร็จรูปสวยแบบภาพต้นฉบับ (include/home_bg.h, ใช้ flash ~215 KB)   0 = วาดกรอบสดแบบเดิม

// ---------- หน้าบูต/รีบูต ----------
#define SPLASH_USE_POSTER 1      // 1 = ภาพป้ายเหลืองหน้าบูต (include/splash_chang.h)   0 = โลโก้ NUI Remap เดิม

// ---------- เชื่อมแอปมือถือ (Wi-Fi) ----------
#define MOBILE_SSID  "NUI-GAUGE"     // ชื่อ Wi-Fi ที่เกจปล่อย
#define MOBILE_PASS  "12345678"      // รหัสผ่าน (ต้องยาว 8 ตัวขึ้นไป) ควรเปลี่ยนเป็นของตัวเอง
#define MOBILE_CH    6               // ช่อง Wi-Fi 1-11
