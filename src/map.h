#pragma once
#include <Arduino.h>
#include "ecu.h"

// ============================================================
//  AFR / INJ / IGT map recorder   (NUI ssk Remap)
//  กริด:  RPM 1200-12000 ทุก 100 rpm  x  TPS 25 จุด (ไม่เท่ากัน ละเอียดช่วงเปิดน้อย)
//  TPS: 0.0 0.4 0.8 1.2 1.8 2.4 3.2 4.4 5.7 7.4 9.2 11.0 13.0 15.0 18.0 22.0
//       26.0 30.0 34.9 40.0 48.1 57.3 66.9 71.9 72.5   (ค่าจริงจาก ECU จะถูกปัดเข้าจุดที่ใกล้ที่สุด)
//  1 ช่อง = ค่าเฉลี่ยเคลื่อนที่ของ AFR / INJ / IGT + จำนวนตัวอย่าง
// ============================================================
constexpr int MAP_RPM_MIN  = 1200;
constexpr int MAP_RPM_MAX  = 12000;
constexpr int MAP_RPM_STEP = 100;
constexpr int MAP_ROWS     = (MAP_RPM_MAX - MAP_RPM_MIN) / MAP_RPM_STEP + 1;   // 109
constexpr int MAP_COLS     = 25;                                               // จำนวนจุดแกน TPS
extern const float MAP_TPS_AXIS[MAP_COLS];                                      // ค่าองศา TPS ของแต่ละคอลัมน์
constexpr int MAP_AVG_WINDOW = 16;     // ช่องที่มีตัวอย่างเกินนี้จะเฉลี่ยแบบถ่วงน้ำหนักล่าสุด 1/16

struct MapAgg { uint16_t cnt = 0; float v[3] = {0, 0, 0}; };   // v: AFR, INJ ms, IGT deg

bool     mapBegin();                       // จองหน่วยความจำ + โหลดไฟล์ที่เคยบันทึก
bool     mapReady();
bool     mapRecord(const EcuData& d);      // true = ตัวอย่างนี้ถูกบันทึกลงช่อง
bool     mapSave();                        // เขียนลง flash (LittleFS)
bool     mapLoad();
void     mapClear(bool removeFile);
uint32_t mapPoints();                      // จำนวนตัวอย่างทั้งหมด
uint32_t mapCells();                       // จำนวนช่องที่มีข้อมูล
int      mapLastRow();
int      mapLastCol();
uint32_t mapRecSeq();                      // เพิ่มทุกครั้งที่บันทึกสำเร็จ
void     mapAggregate(int r0, int r1, int c0, int c1, MapAgg& out);
int      mapRowOfRpm(float rpm);           // -1 ถ้านอกช่วง
int      mapColOfTps(float tpsPct);
void     mapDumpCsv(Print& p);             // ส่งเฉพาะช่องที่มีข้อมูลออก Serial เป็น CSV
