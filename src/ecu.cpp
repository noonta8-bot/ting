#include "ecu.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>

// ============================================================
//  Honda PGM-FI K-Line  (10400 8N1, Big-Endian)
//  ทุกคำสั่ง TX สร้างด้วย checksum = (0x100 - sum) & 0xFF
//  ซึ่งตรงกับค่าในเอกสาร (เช่น 72 05 71 17 01)
// ============================================================

static SemaphoreHandle_t g_mtx = nullptr;
static EcuData g_data;
static DtcInfo g_dtc;

static volatile EcuState g_state = ECU_OFF;
static volatile bool g_enabled = true;
static volatile bool g_demo = false;
static volatile bool g_reconnect = false;
static volatile bool g_cmdDtc = false, g_cmdClear = false, g_cmdReset = false;
static volatile CmdResult g_dtcSt = CMD_NONE, g_clrSt = CMD_NONE, g_rstSt = CMD_NONE;
static volatile float g_rate = 0;
static volatile uint32_t g_err = 0;

// ---------------- ตาราง ----------------
struct DtcDesc { uint16_t v; const char* d; };
static const DtcDesc DTC_TABLE[] = {
  {257, "MAP LOW"},        {258, "MAP HIGH"},
  {1793, "ECT LOW"},       {1794, "ECT HIGH"},
  {2049, "TPS LOW"},       {2050, "TPS HIGH"},
  {2305, "IAT LOW"},       {2306, "IAT HIGH"},
  {2817, "SPEED SENSOR"},  {3073, "INJECTOR #1"},
  {7425, "IACV"},          {8450, "EPROM"},
  {13825, "BANK ANGLE LOW"}, {14593, "OVER HEATING"},
};

void dtcFormat(uint16_t v, char* codeOut, const char** descOut) {
  snprintf(codeOut, 8, "%u-%u", (unsigned)(v >> 8), (unsigned)(v & 0xFF));
  *descOut = "UNKNOWN (see manual)";
  for (size_t i = 0; i < sizeof(DTC_TABLE) / sizeof(DTC_TABLE[0]); i++)
    if (DTC_TABLE[i].v == v) { *descOut = DTC_TABLE[i].d; break; }
}

const char* ecuPartName(uint32_t code, char* buf, size_t n) {
  if (code == 0) snprintf(buf, n, "--");
  else if (code == 65714) snprintf(buf, n, "KWW-601");
  else if (code == 66250) snprintf(buf, n, "K0F-A01/K0F-T01");
  else snprintf(buf, n, "ID %lu", (unsigned long)code);
  return buf;
}

// Lookup AF จากแรงดัน 0-1 V (interpolate เชิงเส้น)
static float afrFromVolts(float v) {
  static const float V[]  = {0.00f, 0.02f, 0.04f, 0.10f, 0.35f, 0.70f, 0.80f, 0.90f, 0.95f, 1.00f};
  static const float AF[] = {20.0f, 18.0f, 17.0f, 16.0f, 15.0f, 14.0f, 13.0f, 12.0f, 11.0f, 10.0f};
  if (v <= V[0]) return AF[0];
  if (v >= V[9]) return AF[9];
  for (int i = 1; i < 10; i++) {
    if (v <= V[i]) {
      float t = (v - V[i - 1]) / (V[i] - V[i - 1]);
      return AF[i - 1] + t * (AF[i] - AF[i - 1]);
    }
  }
  return AF[9];
}

// ตาราง MMODE: 91->4, 96->3, 102->2, 128->1, 135->0 (เลือกค่าที่ใกล้ที่สุด)
static uint8_t modeFromByte(uint8_t x) {
  static const uint8_t K[] = {91, 96, 102, 128, 135};
  static const uint8_t M[] = {4, 3, 2, 1, 0};
  int best = 0, bd = 999;
  for (int i = 0; i < 5; i++) {
    int d = abs((int)x - (int)K[i]);
    if (d < bd) { bd = d; best = i; }
  }
  return M[best];
}

// ---------------- K-Line low level ----------------
static inline uint8_t cks(const uint8_t* d, uint8_t n) {
  uint8_t s = 0;
  for (uint8_t i = 0; i < n; i++) s += d[i];
  return (uint8_t)(0x100 - s);
}

static void mkReq(uint8_t* tx, uint8_t type, uint8_t tbl) {  // 72 05 <type> <tbl> <ck>
  tx[0] = 0x72; tx[1] = 0x05; tx[2] = type; tx[3] = tbl; tx[4] = cks(tx, 4);
}

static void kDrain() { while (Serial2.available()) Serial2.read(); }

static void kBegin() {
  Serial2.setRxBufferSize(256);
  Serial2.begin(K_BAUD, SERIAL_8N1, PIN_K_RX, PIN_K_TX);
}

// ส่ง tx แล้วรับ (echo + reply) ใส่ rx; expectTotal = จำนวนไบต์รวมที่คาดหวัง (0 = ความยาวไม่แน่นอน)
// คืนจำนวนไบต์รวม (รวม echo) หรือ -1 ถ้า echo ผิด
static int kTxRx(const uint8_t* tx, uint8_t txLen, uint8_t* rx, uint8_t rxMax, int expectTotal, uint32_t timeoutMs) {
  kDrain();
  Serial2.write(tx, txLen);
  Serial2.flush();
  int n = 0;
#if !K_HAS_ECHO
  memcpy(rx, tx, txLen);
  n = txLen;
#endif
  uint32_t t0 = millis(), tLast = t0;
  for (;;) {
    while (Serial2.available() && n < rxMax) { rx[n++] = (uint8_t)Serial2.read(); tLast = millis(); }
    if (expectTotal > 0 && n >= expectTotal) break;
    if (n >= rxMax) break;
    uint32_t now = millis();
    if (now - t0 > timeoutMs) break;
    if (expectTotal == 0 && n > txLen && now - tLast > 40) break;
    delay(1);
  }
#if K_HAS_ECHO
  if (n >= txLen && memcmp(rx, tx, txLen) != 0) return -1;
#endif
  return n;
}

static bool replyChecksumOk(const uint8_t* rx, int n, uint8_t txLen) {
#if K_STRICT_CHECKSUM
  uint8_t s = 0;
  for (int i = txLen; i < n; i++) s += rx[i];
  return s == 0;
#else
  (void)rx; (void)n; (void)txLen;
  return true;
#endif
}

static void kInit() {
  kDrain();
  for (int i = 0; i < 90; i++) Serial2.write((uint8_t)0x00);   // 1) 0x00 x90
  Serial2.flush();
  delay(160);                                                    // 2) silence 160 ms
  kDrain();
  const uint8_t a[] = {0x81, 0x12, 0xF1, 0x81};                  // 3)
  Serial2.write(a, sizeof a); Serial2.flush(); delay(160); kDrain();
  const uint8_t b[] = {0xFE, 0x04, 0x72, 0x8C};                  // 4)
  Serial2.write(b, sizeof b); Serial2.flush(); delay(160); kDrain();
}

static void kDisconnect() {
  const uint8_t d[] = {0x72, 0x05, 0x00, 0xF1, 0x98};
  Serial2.write(d, sizeof d); Serial2.flush(); delay(50); kDrain();
}

// ---------------- อ่านตาราง ----------------
static inline uint16_t be16(const uint8_t* p) { return ((uint16_t)p[0] << 8) | p[1]; }

static bool poll17(EcuData& d) {                 // TX 72 05 71 17 01 | RX 29
  uint8_t tx[5], rx[48]; mkReq(tx, 0x71, 0x17);
  int n = kTxRx(tx, 5, rx, sizeof rx, 29, K_REPLY_TIMEOUT_MS);
  if (n < 29 || !replyChecksumOk(rx, n, 5)) return false;
  d.rpm     = be16(&rx[9]);
  d.tpsV    = rx[11] / 255.0f * 5.0f;
  d.tpsPct  = rx[12] * 0.5f;
  d.tpsRaw  = (uint16_t)(rx[12] * 5);
  d.tps100  = constrain(rx[12] * 5.0f * 0.0969f, 0.0f, 100.0f);
  d.eotV    = rx[13] / 255.0f * 5.0f;
  d.eotC    = (float)((int)rx[14] - 40);
  d.batt    = rx[19] * 0.1f;
  d.injMs   = be16(&rx[20]) * 0.003921f;
  d.injDuty = d.injMs * d.rpm * 0.00083333f;
  d.spark   = ((int)rx[22] - 128) * 0.5f;
  d.ltft    = rx[23] / 128.255f;
  d.mode    = modeFromByte(rx[23]);
  return true;
}

static bool poll20(EcuData& d) {                 // TX 72 05 71 20 F8 | RX 13
  uint8_t tx[5], rx[32]; mkReq(tx, 0x71, 0x20);
  int n = kTxRx(tx, 5, rx, sizeof rx, 13, K_REPLY_TIMEOUT_MS);
  if (n < 13 || !replyChecksumOk(rx, n, 5)) return false;
  d.o2V  = (rx[9] / 753.75f) * 14.7f;
  d.afr  = afrFromVolts(d.o2V);
  d.stft = rx[10] / 128.255f;
  return true;
}

static bool pollD1(EcuData& d) {                 // TX 72 05 71 D1 47 | RX 16
  uint8_t tx[5], rx[32]; mkReq(tx, 0x71, 0xD1);
  int n = kTxRx(tx, 5, rx, sizeof rx, 16, K_REPLY_TIMEOUT_MS);
  if (n < 16 || !replyChecksumOk(rx, n, 5)) return false;
  d.neutral = (rx[9] & 0x01) != 0;
  d.scsOn   = (rx[9] & 0x04) == 0;
  d.pump    = (rx[13] & 0x01) != 0;
  return true;
}

static bool readId(EcuData& d) {                 // TX 72 05 71 00 18 | RX 14
  uint8_t tx[5], rx[32]; mkReq(tx, 0x71, 0x00);
  int n = kTxRx(tx, 5, rx, sizeof rx, 14, K_REPLY_TIMEOUT_MS);
  if (n < 14) return false;
  d.partCode = ((uint32_t)rx[8] << 24) | ((uint32_t)rx[9] << 16) | ((uint32_t)rx[10] << 8) | rx[11];
  d.hasId = true;
  return true;
}

static bool readFlash(EcuData& d) {              // TX 7D 06 01 01 03 78 | RX 15
  const uint8_t tx[6] = {0x7D, 0x06, 0x01, 0x01, 0x03, 0x78};
  uint8_t rx[32];
  int n = kTxRx(tx, 6, rx, sizeof rx, 15, K_REPLY_TIMEOUT_MS);
  if (n < 15) return false;
  d.flashCount = rx[0x0C];
  d.hasFlash = true;
  return true;
}

// DTC: ชุดที่ 1-6 เปลี่ยนไบต์ที่ 4 ของคำสั่ง
static bool readDtcSet(uint8_t type, uint8_t set, uint16_t out[3]) {
  uint8_t tx[5], rx[48]; mkReq(tx, type, set);
  for (int t = 0; t < 3; t++) {                  // ลองซ้ำ 3 ครั้งต่อชุด
    int n = kTxRx(tx, 5, rx, sizeof rx, 0, K_REPLY_TIMEOUT_MS);
    if (n >= 16) {
      out[0] = be16(&rx[0x0A]);
      out[1] = be16(&rx[0x0C]);
      out[2] = be16(&rx[0x0E]);
      return true;
    }
    delay(30);
  }
  return false;
}

static bool readDtcList(uint8_t type, DtcList& L) {
  L.count = 0;
  bool any = false;
  for (uint8_t set = 1; set <= 6; set++) {
    uint16_t c[3];
    if (!readDtcSet(type, set, c)) continue;
    any = true;
    for (int i = 0; i < 3; i++) {
      if (c[i] == 0) continue;
      bool dup = false;
      for (uint8_t k = 0; k < L.count; k++) if (L.code[k] == c[i]) dup = true;
      if (!dup && L.count < 18) L.code[L.count++] = c[i];
    }
    delay(10);
  }
  return any;
}

static bool cmdSimple(uint8_t tbl) {             // 72 05 60 <tbl> ck  -> เช็กไบต์ 0x08 == 1
  uint8_t tx[5], rx[32]; mkReq(tx, 0x60, tbl);
  int n = kTxRx(tx, 5, rx, sizeof rx, 0, K_REPLY_TIMEOUT_MS);
  return n > 8 && rx[8] == 1;
}

// ---------------- Demo ----------------
static bool g_demoDtcInit = false;

static void demoFill(EcuData& d) {
  static float rpm = 1400;
  float t = millis() / 1000.0f;
  float th = 0.5f + 0.5f * sinf(t * 0.45f);
  th = th * th;
  float target = 1400 + th * 7800 + 220.0f * sinf(t * 7.0f) * th;
  rpm += (target - rpm) * 0.08f;
  d.rpm = (uint16_t)rpm;
  d.tps100 = constrain(th * 100.0f, 0.0f, 100.0f);
  uint8_t X = (uint8_t)constrain(d.tps100 / 0.4845f, 0.0f, 205.0f);
  d.tpsRaw = X * 5; d.tpsPct = X * 0.5f; d.tpsV = X / 255.0f * 5.0f;
  d.eotC = 40.0f + 58.0f * (1.0f - expf(-t / 90.0f));
  d.eotV = (d.eotC + 40.0f) / 255.0f * 5.0f;
  d.batt = 13.6f + 0.3f * sinf(t * 0.3f);
  d.injMs = 1.8f + th * 5.0f;
  d.injDuty = d.injMs * d.rpm * 0.00083333f;
  d.spark = 10.0f + th * 22.0f;
  d.ltft = 1.0f + 0.02f * sinf(t * 0.2f);
  d.stft = 1.0f + 0.05f * sinf(t * 1.7f);
  d.mode = 1;
  d.o2V = constrain(0.45f + 0.35f * sinf(t * 2.3f), 0.0f, 1.0f);
  d.afr = afrFromVolts(d.o2V);
  d.neutral = fmodf(t, 40.0f) < 6.0f;
  d.scsOn = true; d.pump = true;
  d.speed = d.neutral ? 0.0f : d.rpm * 0.011f; d.hasSpeed = true;
  d.iatC = 31.9f + 4.0f * sinf(t * 0.2f); d.hasIat = true;
  d.mapKpa = 20.0f + d.tpsPct * 1.4f; d.hasMap = true;
  d.partCode = 66250; d.hasId = true; d.flashCount = 3; d.hasFlash = true;
}

// ---------------- Task ----------------
static uint32_t g_seq = 0;
static void publish(const EcuData& d) {
  xSemaphoreTake(g_mtx, portMAX_DELAY);
  g_data = d;
  g_data.seq = ++g_seq;
  xSemaphoreGive(g_mtx);
}

static void handleCommands(bool demo, bool online) {
  if (g_cmdDtc) {
    g_cmdDtc = false;
    DtcInfo di; di.valid = false;
    bool ok;
    if (demo) {
      if (!g_demoDtcInit) { g_demoDtcInit = true; }
      ok = true;
      xSemaphoreTake(g_mtx, portMAX_DELAY); di = g_dtc; xSemaphoreGive(g_mtx);
      if (!di.valid) {                       // ตัวอย่างโค้ดสำหรับโหมด Demo
        di.cur.count = 1; di.cur.code[0] = 2305;
        di.hist.count = 2; di.hist.code[0] = 2050; di.hist.code[1] = 257;
      }
      di.valid = true;
      delay(400);
    } else if (online) {
      ok = readDtcList(0x74, di.cur);
      bool ok2 = readDtcList(0x73, di.hist);
      ok = ok || ok2;
      if (!ok) {                             // อ่านไม่ได้ -> เริ่มเซสชัน K-Line ใหม่เอง (แทนการปิด/เปิดสวิตช์) แล้วลองอีกรอบ
        kInit();
        uint8_t tx[5], rx[48]; mkReq(tx, 0x71, 0x17);
        for (int i = 0; i < 3; i++) if (kTxRx(tx, 5, rx, sizeof rx, 29, K_REPLY_TIMEOUT_MS) >= 29) break;
        ok = readDtcList(0x74, di.cur);
        ok2 = readDtcList(0x73, di.hist);
        ok = ok || ok2;
      }
      di.valid = ok;
    } else ok = false;
    if (ok) { xSemaphoreTake(g_mtx, portMAX_DELAY); g_dtc = di; xSemaphoreGive(g_mtx); }
    g_dtcSt = ok ? CMD_OK : CMD_FAIL;
  }
  if (g_cmdClear) {
    g_cmdClear = false;
    bool ok;
    if (demo) { ok = true; delay(300);
      xSemaphoreTake(g_mtx, portMAX_DELAY); g_dtc = DtcInfo(); g_dtc.valid = true; xSemaphoreGive(g_mtx);
    } else ok = online && cmdSimple(0x01);
    g_clrSt = ok ? CMD_OK : CMD_FAIL;
    if (ok && !demo) g_cmdDtc = true;       // อ่านโค้ดใหม่หลังลบ
  }
  if (g_cmdReset) {
    g_cmdReset = false;
    bool ok = demo ? true : (online && cmdSimple(0x03));
    if (demo) delay(300);
    g_rstSt = ok ? CMD_OK : CMD_FAIL;
  }
}

static void ecuTask(void*) {
  kBegin();
  EcuData local;
  bool connected = false;
  uint8_t fails = 0, slow = 0;
  uint32_t nextTry = 0, rateT = millis();
  uint16_t cycles = 0;

  for (;;) {
    if (g_demo) {
      if (connected) { connected = false; }
      g_state = ECU_DEMO;
      demoFill(local);
      publish(local);
      handleCommands(true, true);
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    if (!g_enabled) {
      if (connected) { kDisconnect(); connected = false; }
      g_state = ECU_OFF;
      local = EcuData(); publish(local);
      handleCommands(false, false);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (g_reconnect) {
      g_reconnect = false;
      if (connected) kDisconnect();
      connected = false; nextTry = 0;
    }

    if (!connected) {
      g_state = ECU_CONNECTING;
      handleCommands(false, false);
      if (millis() < nextTry) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      kInit();
      bool ok = false;
      for (int i = 0; i < 3 && !ok; i++) ok = poll17(local);
      if (ok) {
        connected = true; fails = 0; g_state = ECU_ONLINE;
        poll20(local); pollD1(local);
        readId(local); readFlash(local);
        publish(local);
        g_cmdDtc = true; g_dtcSt = CMD_PENDING;          // อ่าน DTC อัตโนมัติหลังต่อติด
      } else {
        g_err++;
        local = EcuData(); publish(local);
        nextTry = millis() + 1500;
      }
      continue;
    }

    handleCommands(false, true);

    bool ok1 = poll17(local);
    bool ok2 = poll20(local);
    bool ok3 = true;
    if (++slow >= 5) { slow = 0; ok3 = pollD1(local); }

    if (ok1 || ok2) {
      fails = 0;
      publish(local);
      cycles++;
    } else {
      g_err++;
      if (++fails >= 6) {
        connected = false; nextTry = millis() + 1000;
        local = EcuData(); publish(local);
        continue;
      }
    }
    (void)ok3;

    uint32_t now = millis();
    if (now - rateT >= 1000) {
      g_rate = cycles * 1000.0f / (now - rateT);
      cycles = 0; rateT = now;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---------------- API ----------------
void ecuBegin() {
  g_mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(ecuTask, "ecu", 6144, nullptr, 2, nullptr, 0);
}
void ecuGet(EcuData& out) {
  if (!g_mtx) return;
  xSemaphoreTake(g_mtx, portMAX_DELAY); out = g_data; xSemaphoreGive(g_mtx);
}
void ecuGetDtc(DtcInfo& out) {
  if (!g_mtx) return;
  xSemaphoreTake(g_mtx, portMAX_DELAY); out = g_dtc; xSemaphoreGive(g_mtx);
}
EcuState ecuState() { return g_state; }
void ecuSetEnabled(bool en) { g_enabled = en; }
bool ecuEnabled() { return g_enabled; }
void ecuSetDemo(bool on) { g_demo = on; if (on) { g_demoDtcInit = false; xSemaphoreTake(g_mtx, portMAX_DELAY); g_dtc = DtcInfo(); xSemaphoreGive(g_mtx); } }
bool ecuDemo() { return g_demo; }
void ecuReconnect() { g_reconnect = true; }
void ecuReadDtc()  { g_dtcSt = CMD_PENDING; g_cmdDtc = true; }
void ecuClearDtc() { g_clrSt = CMD_PENDING; g_cmdClear = true; }
void ecuResetEcu() { g_rstSt = CMD_PENDING; g_cmdReset = true; }
CmdResult ecuDtcStatus()   { return g_dtcSt; }
CmdResult ecuClearStatus() { return g_clrSt; }
CmdResult ecuResetStatus() { return g_rstSt; }
float ecuRateHz() { return g_rate; }
uint32_t ecuErrors() { return g_err; }
