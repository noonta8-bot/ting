import sys
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
W, H = 275, 220
im = Image.open(src).convert("RGB").resize((W, H), Image.LANCZOS)
px = im.load()
vals = []
for y in range(H):
    for x in range(W):
        r, g, b = px[x, y]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        vals.append(v)
with open(out, "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("// โลโก้ NUI Remap (แปลงจากภาพ) RGB565 %dx%d  — สร้างด้วย tools/img2rgb565.py\n" % (W, H))
    f.write("#define SPLASH_LOGO_W %d\n#define SPLASH_LOGO_H %d\n\n" % (W, H))
    f.write("// ข้อมูลสีเก็บใน flash (PROGMEM) ใช้ tft.pushImage() โดยเปิด setSwapBytes(true)\n")
    f.write("static const uint16_t SPLASH_LOGO[SPLASH_LOGO_W * SPLASH_LOGO_H] PROGMEM = {\n")
    for i in range(0, len(vals), 16):
        f.write("  " + ",".join("0x%04X" % v for v in vals[i:i+16]) + ",\n")
    f.write("};\n")
im.save(out.replace(".h", "_preview.png"))
