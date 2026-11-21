#!/usr/bin/env python3
"""
สร้างพื้นหลังหน้า HOME สำเร็จรูปจากภาพต้นฉบับ  ->  include/home_bg.h
  python3 tools/make_home_bg.py assets/home_target.png include/home_bg.h [--max=12] [--warn=8.5] [--preview=out.png]

สิ่งที่ทำ
  1) ย่อภาพต้นฉบับเป็น 320x240 (RGB565 + ordered dithering กันแถบสี)
  2) ลบ "ค่าที่เปลี่ยนตามเวลา" ออกจากภาพ (ตัวเลข, แถบ AFR/INJ, ข้อความสถานะ, ป้ายแจ้งเตือน, เข็ม RPM)
     แล้วเติมพื้นหลังแผงให้เนียน -> ตอนรันวาดตัวเลขทับ (ฟอนต์ anti-alias, ผสมกับพื้นหลังจริง)
  3) สร้างขีดวัดรอบ RPM ใหม่ตามสเกล --max (0..12) : ภาพ "ดับ" ฝังในพื้นหลัง + ภาพ "ติด" + แผนที่ลำดับขั้น
     ตอนรันแค่คัดลอกพิกเซลสว่างตามรอบเครื่อง -> เกจสวยเท่าภาพ ไม่ต้องคำนวณวงกลม
  4) เก็บภาพกรอบ ECT โทนแดง (เครื่องร้อน) ไว้สลับทับ; พื้นหลังปกติปรับกรอบ ECT เป็นสีม่วงเหมือนเดิม
"""
import sys, math
import numpy as np
import cv2
from PIL import Image, ImageDraw

SRC, OUT = sys.argv[1], sys.argv[2]
ARG = {a.split("=")[0]: a.split("=")[1] for a in sys.argv[3:] if a.startswith("--") and "=" in a}
RPM_MAX_K = float(ARG.get("--max", 12))      # สเกลสูงสุดของเกจ (หน่วยพันรอบ)
WARN_K    = float(ARG.get("--warn", 8.5))    # เริ่มโซนแดง (หน่วยพันรอบ)
PREVIEW   = ARG.get("--preview")

W, H = 320, 240
img0 = Image.open(SRC).convert("RGB")
FW, FH = img0.size
K = W / FW                      # full -> 320
F = FW / W                      # 320 -> full (4.525)

# ---------------- เรขาคณิตเกจ (พิกัด 320x240) ----------------
CX, CY = 164, 102
R_IN, R_OUT = 57, 69            # แถบขีดวัด
LABEL_R = 49.5                  # รัศมีตัวเลข 0..12 (วาดตอนรัน)
A0, SWEEP = -125.0, 230.0       # องศาเริ่ม (0 = ขึ้นบน, หมุนตามเข็ม) / มุมกวาดรวม
NSTEP = 96                      # จำนวนขั้นของแถบไฟ

# ---------------- ช่องตัวเลข (พิกัดภาพเต็ม) : (x0,y0,x1,y1, โหมดลบ) ----------------
CLEAN = {
    "SPEED": ((90, 232, 424, 378), "any"),
    "ECT":   ((205, 520, 400, 652), "redany"),
    "RPM":   ((585, 368, 945, 500), "any"),
    "AFR":   ((1140, 228, 1405, 360), "any"),
    "BAT":   ((1205, 436, 1372, 536), "any"),
    "INJ":   ((1205, 572, 1353, 668), "any"),
    "IAT":   ((45, 805, 394, 1035), "any"),
    "IAT2":  ((394, 805, 470, 982), "any"),
    "TPS":   ((520, 805, 850, 1030), "any"),
    "MAP":   ((1000, 805, 1410, 985), "any"),
    "STATUS":((560, 28, 1030, 110), "any"),
    "CLOCK": ((1200, 22, 1426, 112), "any"),
    "SPLBL": ((50, 150, 352, 238), "any"),
    "AFRBAR":((1088, 360, 1402, 400), "bar"),
    "INJBAR":((1072, 650, 1392, 686), "bar"),
    "BANNER":((530, 552, 992, 705), "any"),
}
# ช่องวาดตัวเลขตอนรัน : ชื่อ -> (ฟอนต์, จัดชิด 0=ซ้าย 1=กลาง 2=ขวา)
DRAW = {"SPEED": ("L", 1), "ECT": ("L", 2), "RPM": ("L", 1), "AFR": ("L", 2), "BAT": ("M", 2),
        "INJ": ("M", 2), "IAT": ("XL", 2), "TPS": ("XL", 2), "MAP": ("XL", 2), "CLOCK": ("M", 2)}

BOX = {"IAT": (45, 812, 388, 992), "TPS": (520, 812, 845, 992), "MAP": (1000, 812, 1405, 995)}   # ช่องวาดจริง (เล็กกว่าพื้นที่ล้าง)

def rect320(r):
    x0, y0, x1, y1 = r
    a, b = int(math.ceil(x0 * K)), int(math.ceil(y0 * K))
    c, d = int(math.floor(x1 * K)), int(math.floor(y1 * K))
    return a, b, c - a, d - b

def clean_region(arr, rect, mode, dil=7):
    x0, y0, x1, y1 = rect
    sub = arr[y0:y1, x0:x1]
    r, g, b = sub[:, :, 0].astype(int), sub[:, :, 1].astype(int), sub[:, :, 2].astype(int)
    mx, mn = sub.max(2).astype(int), sub.min(2).astype(int)
    if mode == "white":   m = mn > 115
    elif mode == "redany": m = ((r > 70) & (r > 1.3 * np.maximum(g, b))) | (mx > 150)
    elif mode == "yellow": m = (r > 150) & (g > 120) & (b < 110)
    elif mode == "bar":   m = mx > 48
    else:                 m = mx > 46
    m = cv2.dilate(m.astype(np.uint8), np.ones((dil, dil), np.uint8)).astype(bool)
    out = sub.copy()
    fills = np.full((sub.shape[0], 3), np.nan)
    for j in range(sub.shape[0]):
        ok = ~m[j]
        if ok.sum() > 6: fills[j] = np.median(sub[j][ok], axis=0)
    # แถวที่ไม่มีพิกเซลว่างเลย -> ใช้ค่าจากแถวใกล้สุด
    good = np.where(~np.isnan(fills[:, 0]))[0]
    if len(good) == 0: fills[:] = 0
    else:
        for j in range(sub.shape[0]):
            if np.isnan(fills[j, 0]): fills[j] = fills[good[np.argmin(abs(good - j))]]
    for c in range(3): fills[:, c] = cv2.GaussianBlur(fills[:, c].reshape(-1, 1).astype(np.float32), (1, 0), 2.0).ravel()
    for j in range(sub.shape[0]): out[j][m[j]] = fills[j]
    arr[y0:y1, x0:x1] = out

def ang_to_xy(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)

# ---------------- 1) ลบค่าตัวแปรออกจากภาพ ----------------
full = np.asarray(img0).copy()
for name, (rc, mode) in CLEAN.items():
    if name == "BANNER":
        sub = full[rc[1]:rc[3], rc[0]:rc[2]]
        m = (sub.max(2) > 40)
        m = cv2.dilate(m.astype(np.uint8), np.ones((9, 9), np.uint8)).astype(bool)
        sub[m] = 0
    else:
        clean_region(full, rc, mode, dil=5 if name == "ECT" else 11)

# วงเกจ : ล้างแถบขีด+ตัวเลขเดิมออก (เฉพาะช่วงมุมของเกจ) แล้ววาดใหม่ตามสเกลที่เลือก
yy, xx = np.mgrid[0:FH, 0:FW]
cxf, cyf = CX * F, CY * F
dx, dy = xx - cxf, yy - cyf
rr = np.hypot(dx, dy)
ang = np.degrees(np.arctan2(dx, -dy))                  # 0 = ขึ้นบน, + = ตามเข็ม
ang = np.where(ang < -180 + 1e-6, ang + 360, ang)
in_sweep = (ang >= A0 - 4) & (ang <= A0 + SWEEP + 4)
wipe = in_sweep & (rr > 183) & (rr < 318)
full[wipe] = 0

# ---------------- 2) วาดแถบขีดวัด (ภาพดับ / ภาพติด) ที่ความละเอียดภาพเต็ม ----------------
def zone_col(frac, lit):
    f_warn = WARN_K / RPM_MAX_K
    if frac < f_warn:
        t = min(1.0, frac / max(f_warn, 1e-6))
        if lit: return (int(0 + 25 * t), int(238 - 128 * t), 255)           # ฟ้าอมเขียว -> น้ำเงิน
        return (35, 105, 255)
    t = (frac - f_warn) / max(1 - f_warn, 1e-6)
    return (255, int(150 - 120 * t), int(10 + 20 * t)) if lit else (230, 40, 40)

def render_ring(lit):
    S = 1.0
    canvas = Image.new("RGB", (FW, FH), (0, 0, 0))
    d = ImageDraw.Draw(canvas)
    rin, rout = R_IN * F, R_OUT * F
    k_base = 0.50 if lit else 0.10
    # พื้นเรืองจาง ๆ (เป็นลิ่มเล็ก ๆ ตามสี)
    n = 240
    for i in range(n):
        a1 = A0 + SWEEP * i / n; a2 = A0 + SWEEP * (i + 1) / n + 0.3
        c = zone_col((i + 0.5) / n, lit); c = tuple(int(v * k_base) for v in c)
        pts = [ang_to_xy(cxf, cyf, rin, a1), ang_to_xy(cxf, cyf, rout, a1), ang_to_xy(cxf, cyf, rout, a2), ang_to_xy(cxf, cyf, rin, a2)]
        d.polygon(pts, fill=c)
    # ขีด: ถี่ทุก 0.2 / กลางทุก 0.5 / ใหญ่ทุก 1.0 (หน่วยพันรอบ)
    steps = int(round(RPM_MAX_K * 10))
    for s in range(steps + 1):
        v = s / 10.0
        a = A0 + SWEEP * v / RPM_MAX_K
        frac = v / RPM_MAX_K
        c = zone_col(frac, lit)
        if abs(v - round(v)) < 1e-6:        r0, w, k = rin, 8, (1.0 if lit else 0.75)
        elif abs(v * 2 - round(v * 2)) < 1e-6: r0, w, k = rin + (rout - rin) * 0.30, 6, (0.95 if lit else 0.55)
        elif s % 2 == 0:                    r0, w, k = rin + (rout - rin) * 0.55, 4, (0.85 if lit else 0.42)
        else: continue
        if not lit and abs(v - round(v)) < 1e-6 and frac < WARN_K / RPM_MAX_K: c = (70, 150, 255)
        col = tuple(min(255, int(x * k)) for x in c)
        p0 = ang_to_xy(cxf, cyf, r0, a); p1 = ang_to_xy(cxf, cyf, rout, a)
        d.line([p0, p1], fill=col, width=w)
    arr = np.asarray(canvas).astype(np.float32)
    glow = cv2.GaussianBlur(arr, (0, 0), 5.0)
    arr = np.clip(arr + glow * (0.9 if lit else 0.5), 0, 255)
    return arr

ring_dim = render_ring(False)
ring_lit = render_ring(True)
band = in_sweep & (rr > 190) & (rr < 320)
full_hot = full.copy()
full_f = full.astype(np.float32)
full_f[band] = np.maximum(full_f[band], ring_dim[band])

# ---------------- 3) กรอบ ECT : ปกติ = ม่วง, เครื่องร้อน = แดงตามภาพต้นฉบับ ----------------
ex0, ey0, ex1, ey1 = 6, 440, 448, 705
hsv = cv2.cvtColor(np.clip(full_f, 0, 255).astype(np.uint8), cv2.COLOR_RGB2HSV)
reg = hsv[ey0:ey1, ex0:ex1]
red = ((reg[:, :, 0] < 9) | (reg[:, :, 0] > 168)) & (reg[:, :, 1] > 90) & (reg[:, :, 2] > 25)
reg[:, :, 0][red] = 140                                # ม่วง (≈280°)
hsv[ey0:ey1, ex0:ex1] = reg
norm = cv2.cvtColor(hsv, cv2.COLOR_HSV2RGB).astype(np.float32)
norm_f = full_f.copy(); norm_f[ey0:ey1, ex0:ex1] = norm[ey0:ey1, ex0:ex1]

# ---------------- 4) ย่อเป็น 320x240 ----------------
def down(a):
    return np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((W, H), Image.LANCZOS)).astype(np.float32)
bg_f = down(norm_f)
hot_f = down(full_f)
ring_lit_320 = down(ring_lit)

BAYER = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
                  [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25], [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]], np.float32)

def to565(a):
    h, w, _ = a.shape
    t = (np.tile(BAYER, (h // 8 + 1, w // 8 + 1))[:h, :w] + 0.5) / 64.0 - 0.5      # -0.5..0.5
    out = np.zeros((h, w), np.uint16)
    q = [(31.0, 8.0), (63.0, 4.0), (31.0, 8.0)]
    ch = []
    for c in range(3):
        lv, st = q[c]
        v = a[:, :, c] / 255.0 * lv + t
        ch.append(np.clip(np.floor(v + 0.5), 0, lv).astype(np.uint16))
    # พื้นดำสนิทต้องดำจริง (ไม่ให้ dither ทิ้งจุดสว่างบนพื้นดำ)
    dark = a.max(2) < 3
    for c in range(3): ch[c][dark] = 0
    return ((ch[0] << 11) | (ch[1] << 5) | ch[2]).astype(np.uint16)

bg565 = to565(bg_f)

# ---------------- 5) แถบวงแหวน (ภาพติด + แผนที่ลำดับขั้น) ----------------
a_end = A0 + SWEEP
xs, ys = [], []
for deg in np.linspace(A0, a_end, 400):
    for r in (R_IN - 1, R_OUT + 1):
        x, y = ang_to_xy(CX + 0.0, CY + 0.0, r, deg); xs.append(x); ys.append(y)
bx0, bx1 = int(math.floor(min(xs))) - 1, int(math.ceil(max(xs))) + 2
by0, by1 = int(math.floor(min(ys))) - 1, int(math.ceil(max(ys))) + 2
RW, RH = bx1 - bx0, by1 - by0
idx = np.full((RH, RW), 255, np.uint8)
lit565 = to565(ring_lit_320[by0:by1, bx0:bx1])
for j in range(RH):
    for i in range(RW):
        px, py = bx0 + i + 0.5 - CX, by0 + j + 0.5 - CY
        r = math.hypot(px, py)
        if r < R_IN - 0.5 or r > R_OUT + 0.5: continue
        a = math.degrees(math.atan2(px, -py))
        if a < -180 + 1e-6: a += 360
        if a < A0 or a > a_end: continue
        idx[j, i] = min(NSTEP - 1, int((a - A0) / SWEEP * NSTEP))
row_min = [int(idx[j][idx[j] != 255].min()) if (idx[j] != 255).any() else 255 for j in range(RH)]
row_max = [int(idx[j][idx[j] != 255].max()) if (idx[j] != 255).any() else 0 for j in range(RH)]

# ---------------- 6) กรอบ ECT แดง (ภาพ hot) ----------------
HX, HY, HW, HH = 1, 97, 100, 60
hot565 = to565(hot_f)[HY:HY + HH, HX:HX + HW]

# ---------------- เขียนไฟล์ ----------------
def arr_c(name, a, per=12, typ="uint16_t", fmt="0x%04X"):
    s = "static const %s %s[] PROGMEM = {\n" % (typ, name)
    flat = [int(v) for v in a.ravel()]
    for i in range(0, len(flat), per):
        s += "  " + ",".join(fmt % v for v in flat[i:i + per]) + ",\n"
    return s + "};\n"

with open(OUT, "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("// สร้างโดย tools/make_home_bg.py จาก assets/home_target.png  (อย่าแก้ด้วยมือ)\n")
    f.write("// ค่าสีเก็บแบบ RGB565 ใน flash : ใช้ tft.setSwapBytes(true) + pushImage()\n\n")
    f.write("#define HB_W %d\n#define HB_H %d\n" % (W, H))
    f.write("#define HB_DIAL_CX %d\n#define HB_DIAL_CY %d\n#define HB_DIAL_RIN %d\n#define HB_DIAL_ROUT %d\n" % (CX, CY, R_IN, R_OUT))
    f.write("#define HB_DIAL_LABEL_R %.1ff\n#define HB_DIAL_A0 %.1ff\n#define HB_DIAL_SWEEP %.1ff\n#define HB_DIAL_NSTEP %d\n" % (LABEL_R, A0, SWEEP, NSTEP))
    f.write("#define HB_DIAL_MAXK %.1ff   // สเกลสูงสุดของเกจ (พันรอบ) ที่ใช้สร้างภาพ -> ต้องตรงกับ RPM_MAX/1000\n" % RPM_MAX_K)
    f.write("#define HB_RING_X %d\n#define HB_RING_Y %d\n#define HB_RING_W %d\n#define HB_RING_H %d\n" % (bx0, by0, RW, RH))
    f.write("#define HB_HOT_X %d\n#define HB_HOT_Y %d\n#define HB_HOT_W %d\n#define HB_HOT_H %d\n" % (HX, HY, HW, HH))
    for name, (rc, mode) in CLEAN.items():
        if name == "IAT2": continue
        x, y, w, h = rect320(BOX.get(name, rc))
        f.write("#define HB_%s_X %d\n#define HB_%s_Y %d\n#define HB_%s_W %d\n#define HB_%s_H %d\n" % (name, x, name, y, name, w, name, h))
    f.write("\n")
    f.write(arr_c("HOME_BG", bg565))
    f.write(arr_c("HOME_RING_LIT", lit565))
    f.write(arr_c("HOME_RING_IDX", idx, 24, "uint8_t", "%d"))
    f.write(arr_c("HOME_RING_RMIN", np.array(row_min), 24, "uint8_t", "%d"))
    f.write(arr_c("HOME_RING_RMAX", np.array(row_max), 24, "uint8_t", "%d"))
    f.write(arr_c("HOME_HOT", hot565))
print("ok", OUT, "bg=%d B  ring=%dx%d (%d B)  hot=%dx%d" % (W * H * 2, RW, RH, RW * RH * 3, HW, HH))

if PREVIEW:
    def to_rgb(v):
        r = ((v >> 11) & 31) * 255 // 31; g = ((v >> 5) & 63) * 255 // 63; b = (v & 31) * 255 // 31
        return np.stack([r, g, b], -1).astype(np.uint8)
    Image.fromarray(to_rgb(bg565)).resize((960, 720), Image.NEAREST).save(PREVIEW)
