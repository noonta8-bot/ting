#include "mobile.h"
#include "mirror.h"
#include "ui.h"
#include "config.h"
#include "web_index.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/message_buffer.h>

#define XSTR_(x) #x
#define STR_(x) XSTR_(x)

// ---------------- สถานะที่ใช้ร่วมกัน ----------------
static volatile int      g_clients = 0;       // จำนวนมือถือที่ต่ออยู่
static volatile bool     g_enabled = true;    // เปิด Wi-Fi ไว้ไหม (จำใน flash)
static volatile bool     g_overflow = false;  // คิวภาพเต็ม -> ต้องวาดหน้าจอส่งใหม่ทั้งหน้า
static volatile float    g_kmh = 0;           // ความเร็ว GPS ล่าสุดจากมือถือ
static volatile uint32_t g_kmhT = 0;          // เวลาที่รับ (0 = ไม่มีสัญญาณ GPS)

enum EvType : uint8_t { EV_TAP, EV_SWIPE, EV_RESYNC };
struct Ev { uint8_t t; int16_t a, b, c, d; };
static QueueHandle_t         g_evq = nullptr;      // มือถือ (task เครือข่าย) -> UI (loop)
static MessageBufferHandle_t g_mb = nullptr;       // UI (loop) -> task เครือข่าย : ชุดคำสั่งวาด

static WebServer        g_http(80);
static WebSocketsServer g_ws(81);
static Preferences      g_pref;
static bool             g_netUp = false;

// ---------------- ส่วนที่ mirror.cpp เรียก ----------------
bool mobileActive() { return g_clients > 0; }
bool mobileSink(const uint8_t* d, size_t n) {
  if (!g_mb || !n) return false;
  // หน้า HOME ทั้งหน้าใหญ่กว่าคิว (~170 KB) -> รอให้ task เครือข่ายระบายออกได้สูงสุด 60 ms ต่อก้อน (เกิดเฉพาะตอนเปลี่ยนหน้า/เพิ่งต่อ)
  if (xMessageBufferSend(g_mb, d, n, pdMS_TO_TICKS(60)) == 0) { g_overflow = true; return false; }
  return true;
}

// ---------------- API ----------------
int  mobileClients() { return g_clients; }
bool mobileEnabled() { return g_enabled; }
void mobileSetEnabled(bool on) {
  g_enabled = on;
  g_pref.putBool("on", on);
}
bool mobileSpeed(float& kmh) {
  uint32_t t = g_kmhT;
  if (!t || (int32_t)(millis() - t) > 3000) return false;
  kmh = g_kmh;
  return true;
}

static void postEv(uint8_t t, int a = 0, int b = 0, int c = 0, int d = 0) {
  if (!g_evq) return;
  Ev e = {t, (int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d};
  xQueueSend(g_evq, &e, 0);
}

// ---------------- WebSocket : รับคำสั่งจากมือถือ (ข้อความธรรมดา) ----------------
//   "T x y"            แตะ
//   "W dx dy x0 y0"    ปัด
//   "S kmh"            ความเร็ว GPS (km/h) ; "S -1" = ไม่มีสัญญาณ GPS
//   "R"                ขอวาดหน้าจอส่งใหม่
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static void onText(const char* s) {
  int a, b, c, d;
  switch (s[0]) {
    case 'T': if (sscanf(s + 1, "%d %d", &a, &b) == 2) postEv(EV_TAP, clampi(a, 0, SCREEN_W - 1), clampi(b, 0, SCREEN_H - 1)); break;
    case 'W': if (sscanf(s + 1, "%d %d %d %d", &a, &b, &c, &d) == 4)
                postEv(EV_SWIPE, clampi(a, -400, 400), clampi(b, -400, 400), clampi(c, 0, SCREEN_W - 1), clampi(d, 0, SCREEN_H - 1)); break;
    case 'S': { float v = atof(s + 1); if (v < 0 || v > 400) { g_kmhT = 0; } else { g_kmh = v; g_kmhT = millis() | 1; } } break;
    case 'R': postEv(EV_RESYNC); break;
    default: break;
  }
}
static void onWsEvent(uint8_t num, WStype_t type, uint8_t* p, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      g_ws.sendTXT(num, "{\"t\":\"hello\",\"w\":" STR_(SCREEN_W) ",\"h\":" STR_(SCREEN_H) ",\"v\":1}");
      g_clients = g_clients + 1;                       // นับหลังส่ง hello เพื่อให้คำสั่งวาดมาหลังข้อความแรกเสมอ
      postEv(EV_RESYNC);                               // ให้เกจวาดหน้าปัจจุบันซ้ำทั้งหน้า -> มือถือได้ภาพครบ
    } break;
    case WStype_DISCONNECTED:
      if (g_clients > 0) g_clients = g_clients - 1;
      if (g_clients == 0) g_kmhT = 0;
      break;
    case WStype_TEXT: {
      char buf[48]; size_t n = len < sizeof buf - 1 ? len : sizeof buf - 1;
      memcpy(buf, p, n); buf[n] = 0;
      onText(buf);
    } break;
    default: break;
  }
}

// ---------------- เครือข่าย ----------------
static void netStart() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(MOBILE_SSID, MOBILE_PASS, MOBILE_CH, 0, 2);          // สูงสุด 2 เครื่อง
  WiFi.setSleep(false);                                            // ลดหน่วงของ Wi-Fi
  g_http.on("/", HTTP_GET, []() { g_http.sendHeader("Cache-Control", "no-cache"); g_http.send_P(200, "text/html", INDEX_HTML); });
  g_http.onNotFound([]() { g_http.send(404, "text/plain", "not found"); });
  g_http.begin();
  g_ws.begin();
  g_ws.onEvent(onWsEvent);
  g_ws.enableHeartbeat(15000, 3000, 2);                            // ตัดมือถือที่หายเงียบ ๆ
  g_netUp = true;
}
static void netStop() {
  g_clients = 0; g_kmhT = 0;
  g_ws.close();
  g_http.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_netUp = false;
}

static uint8_t g_rx[8300];                                         // ก้อนที่ดึงจากคิวเพื่อส่งออก
static void netPump() {
  if (g_enabled != g_netUp) { if (g_enabled) netStart(); else netStop(); }
  if (!g_netUp) return;
  g_http.handleClient();
  g_ws.loop();
  size_t n;
  while ((n = xMessageBufferReceive(g_mb, g_rx, sizeof g_rx, 0)) > 0)
    if (g_clients > 0) g_ws.broadcastBIN(g_rx, n);
  static uint32_t lastOv = 0;
  if (g_overflow) {                                                // คิวเต็ม (มือถือช้า) -> วาดใหม่ทั้งหน้า แต่ไม่ถี่เกิน 3 วิ
    g_overflow = false;
    if (millis() - lastOv > 3000) { lastOv = millis(); postEv(EV_RESYNC); }
  }
}
#ifdef MOBILE_TEST
void mobileTestPump() { netPump(); }
#endif
static void netTask(void*) {
  for (;;) { netPump(); vTaskDelay(pdMS_TO_TICKS(3)); }
}

void mobileBegin() {
  g_pref.begin("mobile", false);
  g_enabled = g_pref.getBool("on", true);
  g_evq = xQueueCreate(24, sizeof(Ev));
  g_mb  = xMessageBufferCreate(16384);
  xTaskCreatePinnedToCore(netTask, "mobile", 8192, nullptr, 1, nullptr, 0);   // core 0 (ร่วมกับ Wi-Fi) ความสำคัญต่ำกว่า task ECU
}

void mobileLoop(uint32_t now) {
  mirrorFlush(now);
  if (!g_evq) return;
  Ev e; static uint32_t lastRs = 0;
  while (xQueueReceive(g_evq, &e, 0) == pdTRUE) {
    switch (e.t) {
      case EV_TAP:   uiTap(e.a, e.b); break;
      case EV_SWIPE: uiSwipe(e.a, e.b, e.c, e.d); break;
      case EV_RESYNC: if (now - lastRs > 300) { lastRs = now; uiForceRedraw(); } break;
    }
  }
}
