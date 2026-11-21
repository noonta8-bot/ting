#!/usr/bin/env python3
"""สร้าง include/race_font.h  (ฟอนต์อังกฤษ+ตัวเลขสไตล์สปอร์ต/ซิ่ง แบบบิตแมป 1bpp, ASCII 0x20-0x7E)
ต้นฉบับ: Inter ExtraBold Italic (SIL OFL)  -  ตัวเลขทุกตัวกว้างเท่ากัน (tabular) ค่าจะได้ไม่สั่นตอนเลขเปลี่ยน
ใช้:  python3 tools/gen_race_font.py /usr/share/fonts/opentype/inter/Inter-ExtraBoldItalic.otf include/race_font.h
"""
import sys
from PIL import Image, ImageDraw, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
SIZES = [("S", 11), ("M", 15), ("L", 24)]      # เล็ก (font 1) / กลาง (font 2) / ใหญ่ (font 4)
CODES = list(range(0x20, 0x7F))
THRESH = 118

def glyphs_for(px):
    font = ImageFont.truetype(SRC, px, layout_engine=ImageFont.Layout.BASIC)
    adv = {cp: int(round(font.getlength(chr(cp)))) for cp in CODES}
    dmax = max(adv[c] for c in range(ord('0'), ord('9') + 1))
    out, bmp = [], bytearray()
    for cp in CODES:
        ch = chr(cp)
        a = dmax if ch.isdigit() else adv[cp]
        if ch == ' ': a = max(a, int(round(px * 0.36)))      # เว้นวรรคให้เห็นชัด (S=4, M=5, L=9)
        W = H = px * 3
        im = Image.new("L", (W, H), 0)
        ox, oy = px, px * 2
        ImageDraw.Draw(im).text((ox, oy), ch, font=font, fill=255, anchor="ls")
        bw = im.point(lambda v: 255 if v >= THRESH else 0)
        bb = bw.getbbox()
        if not bb:
            out.append((len(bmp), 0, 0, 0, 0, a)); continue
        x0, y0, x1, y1 = bb
        w, h = x1 - x0, y1 - y0
        off = len(bmp)
        for yy in range(y0, y1):
            nb = (w + 7) // 8
            for b in range(nb):
                v = 0
                for k in range(8):
                    xx = x0 + b * 8 + k
                    v = (v << 1) | (1 if (b * 8 + k < w and bw.getpixel((xx, yy))) else 0)
                bmp.append(v)
        xo = x0 - ox
        if ch.isdigit(): xo += (a - adv[cp]) // 2        # จัดตัวเลขกึ่งกลางช่องกว้างเท่ากัน
        out.append((off, w, h, xo, y0 - oy, a))
    asc = max(-g[4] for g in out if g[1]); desc = max(g[4] + g[2] for g in out if g[1])
    return out, bmp, asc, max(desc, 0)

with open(OUT, "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n#include \"thai_font.h\"   // ใช้โครงสร้าง ThGlyph / ThFont ร่วมกัน\n\n")
    f.write("// ฟอนต์อังกฤษ+ตัวเลขสไตล์สปอร์ต (บิตแมป) สร้างด้วย tools/gen_race_font.py (ต้นฉบับ: %s)\n" % SRC.split('/')[-1])
    f.write("// ดัชนี: ASCII 0x20-0x7E = 0..94\n\n")
    for name, px in SIZES:
        g, bmp, asc, desc = glyphs_for(px)
        f.write("static const uint8_t RF_%s_BMP[] PROGMEM = {\n" % name)
        for i in range(0, len(bmp), 24):
            f.write("  " + ",".join("0x%02X" % b for b in bmp[i:i+24]) + ",\n")
        f.write("};\n")
        f.write("static const ThGlyph RF_%s_G[] PROGMEM = {\n" % name)
        for (off, w, h, xo, yo, adv) in g:
            f.write("  {%d,%d,%d,%d,%d,%d},\n" % (off, w, h, xo, yo, adv))
        f.write("};\n")
        f.write("static const ThFont RF_%s = {RF_%s_BMP, RF_%s_G, %d, %d};\n\n" % (name, name, name, asc, desc))
print("ok", OUT)
