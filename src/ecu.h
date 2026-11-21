#pragma once
#include <Arduino.h>

// ข้อมูลเครื่องยนต์ที่แปลงเป็นค่าจริงแล้วทั้งหมด
struct EcuData {
  // TABLE 17
  uint16_t rpm = 0;
  float tpsV = 0;        // V
  float tpsPct = 0;      // X*0.5  (0-72.5)
  float tps100 = 0;      // สเกล 100%
  uint16_t tpsRaw = 0;   // 0-1024
  float eotV = 0;        // V
  float eotC = 0;        // C
  float batt = 0;        // V
  float injMs = 0;       // ms
  float injDuty = 0;     // %
  float spark = 0;       // deg TDC
  float ltft = 1.0f;     // ratio
  uint8_t mode = 0;      // MMODE
  // TABLE 20
  float o2V = 0;         // V
  float afr = 0;         // AF (จากตาราง Lookup)
  float stft = 1.0f;
  // TABLE D1
  bool neutral = false;
  bool scsOn = false;
  bool pump = false;
  // อื่นๆ
  uint32_t partCode = 0; bool hasId = false;
  uint8_t flashCount = 0; bool hasFlash = false;
  // ความเร็ว (km/h): ยังไม่มีไบต์ความเร็วใน TABLE ที่อ่านอยู่ -> hasSpeed=false จะแสดง "--" (โหมด DEMO จำลองให้)
  float speed = 0; bool hasSpeed = false;
  // IAT / MAP : ยังไม่ได้ map ไบต์จาก ECU -> hasIat/hasMap=false แสดง "--" (โหมด DEMO จำลองให้)
  float iatC = 0, mapKpa = 0; bool hasIat = false, hasMap = false;
  uint32_t seq = 0;      // เพิ่มทุกครั้งที่ได้ข้อมูลชุดใหม่จาก ECU (ใช้บันทึกตาราง MAP ตามอัตราจริง)
};

enum EcuState : uint8_t { ECU_OFF, ECU_CONNECTING, ECU_ONLINE, ECU_DEMO };
enum CmdResult : int8_t { CMD_NONE = 0, CMD_PENDING, CMD_OK, CMD_FAIL };

struct DtcList { uint8_t count = 0; uint16_t code[18] = {0}; };
struct DtcInfo { bool valid = false; DtcList cur; DtcList hist; };

void     ecuBegin();                       // สร้าง task อ่าน ECU (core 0)
void     ecuGet(EcuData& out);
EcuState ecuState();
void     ecuSetEnabled(bool en);
bool     ecuEnabled();
void     ecuSetDemo(bool on);
bool     ecuDemo();
void     ecuReconnect();

void     ecuReadDtc();
void     ecuClearDtc();
void     ecuResetEcu();
void     ecuGetDtc(DtcInfo& out);
CmdResult ecuDtcStatus();
CmdResult ecuClearStatus();
CmdResult ecuResetStatus();

float    ecuRateHz();
uint32_t ecuErrors();

void        dtcFormat(uint16_t v, char* codeOut /*>=8*/, const char** descOut);
const char* ecuPartName(uint32_t code, char* buf, size_t n);
