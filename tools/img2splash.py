#!/usr/bin/env python3
"""
แปลงภาพโปสเตอร์ (แนวตั้ง) -> หน้าจอบูต/รีบูต 320x240 RGB565
  python3 tools/img2splash.py ภาพ.jpg include/splash_chang.h [--no-dither] [--size=340x240]

- ภาพ 4:3 (เช่น 1152x864) ย่อเต็มจอพอดี; ภาพสัดส่วนอื่นถูกย่อให้พอดีความสูงจอ (ไม่ตัด ไม่บิดสัดส่วน) วางกลางจอ
- สองข้างต่อขยายด้วยสีจากขอบภาพจริง (สะท้อน + เบลอ) เพื่อให้โทนสีต่อเนื่อง
- ปัดค่าสีเป็น RGB565 แบบ "ปัดเศษใกล้สุด" (ไม่ตัดทิ้ง) + Floyd-Steinberg (serpentine)
  เพื่อให้สีที่เห็นบนจอใกล้ต้นฉบับที่สุดเท่าที่จอ 16 บิตทำได้
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

SW, SH = 320, 240
for _a in sys.argv[1:]:                      # --size=340x240
    if _a.startswith("--size="):
        SW, SH = map(int, _a[7:].lower().split("x"))

def build_canvas(src):
    im = Image.open(src).convert("RGB")
    w, h = im.size
    nw = round(w * SH / h)                     # ย่อให้พอดีความสูง
    if abs(nw - SW) <= 2:                      # ภาพ 4:3 พอดีจอ -> เต็มจอเลย ไม่ต้องมีแถบข้าง
        return im.resize((SW, SH), Image.LANCZOS), 0, SW
    main = im.resize((nw, SH), Image.LANCZOS)
    x0 = (SW - nw) // 2
    canvas = Image.new("RGB", (SW, SH))
    # ต่อขยายด้านข้าง: สะท้อนขอบภาพแล้วเบลอ
    big = Image.new("RGB", (SW + nw * 2, SH))
    mir = main.transpose(Image.FLIP_LEFT_RIGHT)
    reps = (SW // nw) + 3
    strip = Image.new("RGB", (nw * reps, SH))
    for i in range(reps):
        strip.paste(main if i % 2 == 0 else mir, (i * nw, 0))
    # แผ่นพื้นหลังเต็มจอ ใช้สะท้อนซ้าย/ขวาจากขอบภาพ
    bg = Image.new("RGB", (SW, SH))
    left_w = x0
    right_w = SW - x0 - nw
    left = Image.new("RGB", (left_w, SH))
    right = Image.new("RGB", (right_w, SH))
    # ซ้าย: สะท้อนจากคอลัมน์ซ้ายสุดของภาพออกไป
    src_l = main.crop((0, 0, min(nw, left_w), SH)).transpose(Image.FLIP_LEFT_RIGHT)
    left.paste(src_l, (left_w - src_l.width, 0))
    if src_l.width < left_w:
        left.paste(src_l.transpose(Image.FLIP_LEFT_RIGHT), (0, 0))
    src_r = main.crop((nw - min(nw, right_w), 0, nw, SH)).transpose(Image.FLIP_LEFT_RIGHT)
    right.paste(src_r, (0, 0))
    bg.paste(left, (0, 0)); bg.paste(right, (x0 + nw, 0))
    bg = bg.filter(ImageFilter.GaussianBlur(7))
    if "--black-pad" in sys.argv: bg = Image.new("RGB", (SW, SH), (0, 0, 0))
    canvas.paste(bg, (0, 0))
    canvas.paste(main, (x0, 0))                # ส่วนภาพจริงคมชัด ไม่เบลอ
    return canvas, x0, nw

def to565(img, dither=True):
    a = np.asarray(img, dtype=np.float32).copy()
    h, w, _ = a.shape
    mx = np.array([31.0, 63.0, 31.0], dtype=np.float32)
    codes = np.zeros((h, w, 3), dtype=np.int32)
    for y in range(h):
        rng = range(w) if (y % 2 == 0 or not dither) else range(w - 1, -1, -1)
        d = 1 if (y % 2 == 0 or not dither) else -1
        for x in rng:
            old = a[y, x]
            q = np.clip(np.rint(old / 255.0 * mx), 0, mx)
            codes[y, x] = q
            if dither:
                err = old - q / mx * 255.0
                if 0 <= x + d < w: a[y, x + d] += err * 7 / 16
                if y + 1 < h:
                    if 0 <= x - d < w: a[y + 1, x - d] += err * 3 / 16
                    a[y + 1, x] += err * 5 / 16
                    if 0 <= x + d < w: a[y + 1, x + d] += err * 1 / 16
    vals = (codes[..., 0] << 11) | (codes[..., 1] << 5) | codes[..., 2]
    prev = np.rint(codes / mx * 255.0).astype(np.uint8)
    return vals.astype(np.uint16), Image.fromarray(prev)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dither = "--no-dither" not in sys.argv
    src, out = args[0], args[1]
    canvas, x0, nw = build_canvas(src)
    vals, prev = to565(canvas, dither)
    with open(out, "w") as f:
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write("// หน้าบูต/รีบูต (ป้ายสามเหลี่ยมเหลือง) RGB565 %dx%d — สร้างด้วย tools/img2splash.py\n" % (SW, SH))
        f.write("#define SPLASH_IMG_W %d\n#define SPLASH_IMG_H %d\n" % (SW, SH))
        f.write("#define SPLASH_IMG_X0 %d   // ขอบซ้ายของภาพโปสเตอร์จริง (ซ้ายขวาเป็นพื้นต่อขยาย)\n" % x0)
        f.write("#define SPLASH_IMG_POSTER_W %d\n\n" % nw)
        f.write("// เก็บใน flash (PROGMEM) ใช้ tft.pushImage() โดยเปิด setSwapBytes(true)\n")
        f.write("static const uint16_t SPLASH_IMG[SPLASH_IMG_W * SPLASH_IMG_H] PROGMEM = {\n")
        flat = vals.flatten()
        for i in range(0, len(flat), 16):
            f.write("  " + ",".join("0x%04X" % v for v in flat[i:i+16]) + ",\n")
        f.write("};\n")
    prev.save(out.replace(".h", "_preview.png"))
    print("ok", out, "poster x0=%d w=%d" % (x0, nw))

main()
