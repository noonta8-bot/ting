#include "map.h"
#include "config.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

const float MAP_TPS_AXIS[MAP_COLS] = {
  0.0f, 0.4f, 0.8f, 1.2f, 1.8f, 2.4f, 3.2f, 4.4f, 5.7f, 7.4f, 9.2f, 11.0f, 13.0f,
  15.0f, 18.0f, 22.0f, 26.0f, 30.0f, 34.9f, 40.0f, 48.1f, 57.3f, 66.9f, 71.9f, 72.5f
};

constexpr size_t MAP_N = (size_t)MAP_ROWS * MAP_COLS;

static uint8_t *aAfr = nullptr;   // AFR x10        (0.1 A/F)
static uint8_t *aInj = nullptr;   // INJ ms x16     (0.0625 ms)
static uint8_t *aIgt = nullptr;   // (spark+20) x4  (0.25 deg, -20..43.75)
static uint8_t *aCnt = nullptr;   // จำนวนตัวอย่าง (อิ่มตัวที่ 255)
static uint32_t g_pts = 0, g_cells = 0, g_recSeq = 0;
static int g_lastR = -1, g_lastC = -1;

struct MapHdr {
  char magic[4];
  uint8_t ver, pad;
  uint16_t rows, cols;
  uint32_t pts, cells;
};

static void* alloc8(size_t n) { return heap_caps_calloc(n, 1, MALLOC_CAP_8BIT); }

bool mapReady() { return aCnt != nullptr; }

bool mapBegin() {
  aAfr = (uint8_t*)alloc8(MAP_N); aInj = (uint8_t*)alloc8(MAP_N);
  aIgt = (uint8_t*)alloc8(MAP_N); aCnt = (uint8_t*)alloc8(MAP_N);
  if (!aAfr || !aInj || !aIgt || !aCnt) {
    free(aAfr); free(aInj); free(aIgt); free(aCnt);
    aAfr = aInj = aIgt = aCnt = nullptr;
    return false;
  }
  mapLoad();
  return true;
}

// เฉลี่ยแบบเคลื่อนที่ + ปัดเศษแบบสุ่ม (ไม่เอนเอียง) เพื่อให้เก็บ 1 ไบต์ได้โดยค่าไม่ค้าง
static uint8_t blend(uint8_t old, float x, uint8_t n) {
  float w = 1.0f / (float)(n < MAP_AVG_WINDOW ? n + 1 : MAP_AVG_WINDOW);
  float v = (float)old + (x - (float)old) * w;
  if (v < 0) v = 0; if (v > 255) v = 255;
  int iv = (int)v;
  float fr = v - (float)iv;
  if ((float)(esp_random() & 0xFFFF) < fr * 65536.0f) iv++;
  return (uint8_t)(iv > 255 ? 255 : iv);
}

int mapRowOfRpm(float rpm) {
  if (rpm < MAP_RPM_MIN - MAP_RPM_STEP / 2 || rpm >= MAP_RPM_MAX + MAP_RPM_STEP / 2) return -1;
  int r = (int)((rpm - MAP_RPM_MIN + MAP_RPM_STEP / 2) / MAP_RPM_STEP);
  return r < 0 ? 0 : (r >= MAP_ROWS ? MAP_ROWS - 1 : r);
}
int mapColOfTps(float tps) {
  int best = 0; float bd = fabsf(tps - MAP_TPS_AXIS[0]);
  for (int i = 1; i < MAP_COLS; i++) {            // หาจุดแกน TPS ที่ใกล้ที่สุด
    float d = fabsf(tps - MAP_TPS_AXIS[i]);
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

bool mapRecord(const EcuData& d) {
  if (!aCnt) return false;
  int r = mapRowOfRpm((float)d.rpm);
  if (r < 0) return false;
  if (d.afr < 8.0f || d.afr > 25.0f) return false;
  int c = mapColOfTps(d.tpsPct);
  size_t i = (size_t)r * MAP_COLS + c;
  uint8_t n = aCnt[i];
  aAfr[i] = blend(aAfr[i], d.afr * 10.0f, n);
  aInj[i] = blend(aInj[i], d.injMs * 16.0f, n);
  aIgt[i] = blend(aIgt[i], (d.spark + 20.0f) * 4.0f, n);
  if (n == 0) g_cells++;
  if (n < 255) aCnt[i] = n + 1;
  g_pts++; g_recSeq++;
  g_lastR = r; g_lastC = c;
  return true;
}

void mapAggregate(int r0, int r1, int c0, int c1, MapAgg& o) {
  o = MapAgg();
  if (!aCnt) return;
  float s0 = 0, s1 = 0, s2 = 0; uint32_t w = 0;
  for (int r = r0; r <= r1; r++) {
    size_t base = (size_t)r * MAP_COLS;
    for (int c = c0; c <= c1; c++) {
      size_t i = base + c; uint8_t n = aCnt[i];
      if (!n) continue;
      s0 += (float)aAfr[i] * n; s1 += (float)aInj[i] * n; s2 += (float)aIgt[i] * n; w += n;
    }
  }
  if (!w) return;
  o.cnt = w > 65535 ? 65535 : (uint16_t)w;
  o.v[0] = s0 / w / 10.0f;
  o.v[1] = s1 / w / 16.0f;
  o.v[2] = s2 / w / 4.0f - 20.0f;
}

uint32_t mapPoints() { return g_pts; }
uint32_t mapCells()  { return g_cells; }
int mapLastRow() { return g_lastR; }
int mapLastCol() { return g_lastC; }
uint32_t mapRecSeq() { return g_recSeq; }

void mapClear(bool removeFile) {
  if (aCnt) { memset(aAfr, 0, MAP_N); memset(aInj, 0, MAP_N); memset(aIgt, 0, MAP_N); memset(aCnt, 0, MAP_N); }
  g_pts = g_cells = 0; g_lastR = g_lastC = -1; g_recSeq++;
  if (removeFile && LittleFS.begin(true)) LittleFS.remove(MAP_FILE);
}

bool mapSave() {
  if (!aCnt || !LittleFS.begin(true)) return false;
  File f = LittleFS.open(MAP_FILE, "w");
  if (!f) return false;
  MapHdr h = {{'N', 'S', 'M', 'P'}, 2, 0, (uint16_t)MAP_ROWS, (uint16_t)MAP_COLS, g_pts, g_cells};
  bool ok = f.write((const uint8_t*)&h, sizeof h) == sizeof h;
  ok = ok && f.write(aAfr, MAP_N) == MAP_N;
  ok = ok && f.write(aInj, MAP_N) == MAP_N;
  ok = ok && f.write(aIgt, MAP_N) == MAP_N;
  ok = ok && f.write(aCnt, MAP_N) == MAP_N;
  f.close();
  return ok;
}

bool mapLoad() {
  if (!aCnt || !LittleFS.begin(true)) return false;
  if (!LittleFS.exists(MAP_FILE)) return false;
  File f = LittleFS.open(MAP_FILE, "r");
  if (!f) return false;
  MapHdr h;
  bool ok = f.read((uint8_t*)&h, sizeof h) == sizeof h
         && memcmp(h.magic, "NSMP", 4) == 0 && h.ver == 2 && h.rows == MAP_ROWS && h.cols == MAP_COLS;
  ok = ok && f.read(aAfr, MAP_N) == MAP_N;
  ok = ok && f.read(aInj, MAP_N) == MAP_N;
  ok = ok && f.read(aIgt, MAP_N) == MAP_N;
  ok = ok && f.read(aCnt, MAP_N) == MAP_N;
  f.close();
  if (!ok) { mapClear(false); return false; }
  g_pts = h.pts; g_cells = 0;
  for (size_t i = 0; i < MAP_N; i++) if (aCnt[i]) g_cells++;
  g_recSeq++;
  return true;
}

void mapDumpCsv(Print& p) {
  if (!aCnt) return;
  p.println("rpm,tps,afr,inj_ms,igt_deg,count");
  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) {
      size_t i = (size_t)r * MAP_COLS + c;
      if (!aCnt[i]) continue;
      p.printf("%d,%.1f,%.1f,%.2f,%.2f,%u\n", MAP_RPM_MIN + r * MAP_RPM_STEP, MAP_TPS_AXIS[c],
               aAfr[i] / 10.0f, aInj[i] / 16.0f, aIgt[i] / 4.0f - 20.0f, (unsigned)aCnt[i]);
    }
    delay(1);
  }
}
