#!/usr/bin/env python3
"""สร้าง include/thai_font.h  (ฟอนต์ไทย+ASCII แบบบิตแมป 1bpp) จากฟอนต์ TTF/OTF
ใช้:  python3 tools/gen_thai_font.py /usr/share/fonts/opentype/tlwg/Loma.otf include/thai_font.h
"""
import sys
from PIL import Image, ImageDraw, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
SIZES = [("S", 12), ("M", 15), ("L", 22)]          # ขนาดเล็ก (font 1) / กลาง (font 2) / ใหญ่ (font 4)
CODES = list(range(0x20, 0x7F)) + list(range(0x0E01, 0x0E5C))
THRESH = 118

def glyphs_for(px):
    font = ImageFont.truetype(SRC, px, layout_engine=ImageFont.Layout.BASIC)
    asc, desc = font.getmetrics()
    out, bmp = [], bytearray()
    for cp in CODES:
        ch = chr(cp)
        adv = int(round(font.getlength(ch)))
        W = H = px * 3
        im = Image.new("L", (W, H), 0)
        ox, oy = px, px * 2                          # จุดอ้างอิง (baseline)
        ImageDraw.Draw(im).text((ox, oy), ch, font=font, fill=255, anchor="ls")
        bw = im.point(lambda v: 255 if v >= THRESH else 0)
        bb = bw.getbbox()
        if not bb:
            out.append((len(bmp), 0, 0, 0, 0, adv)); continue
        x0, y0, x1, y1 = bb
        w, h = x1 - x0, y1 - y0
        off = len(bmp)
        for yy in range(y0, y1):
            row = 0; nb = (w + 7) // 8
            for b in range(nb):
                v = 0
                for k in range(8):
                    xx = x0 + b * 8 + k
                    v = (v << 1) | (1 if (b * 8 + k < w and bw.getpixel((xx, yy))) else 0)
                bmp.append(v)
        out.append((off, w, h, x0 - ox, y0 - oy, adv))
    asc = max(-g[4] for g in out if g[1]); desc = max(g[4] + g[2] for g in out if g[1])   # ขอบเขตหมึกจริง
    return out, bmp, asc, max(desc, 0)

with open(OUT, "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("// ฟอนต์ไทย + ASCII แบบบิตแมป สร้างด้วย tools/gen_thai_font.py (ต้นฉบับ: %s)\n" % SRC.split('/')[-1])
    f.write("// ดัชนี: ASCII 0x20-0x7E = 0..94 ; ไทย 0x0E01-0x0E5B = 95..\n")
    f.write("struct ThGlyph { uint16_t off; uint8_t w, h; int8_t xo, yo; uint8_t adv; };\n")
    f.write("struct ThFont { const uint8_t* bmp; const ThGlyph* g; uint8_t asc, desc; };\n\n")
    for name, px in SIZES:
        g, bmp, asc, desc = glyphs_for(px)
        f.write("static const uint8_t TH_%s_BMP[] PROGMEM = {\n" % name)
        for i in range(0, len(bmp), 24):
            f.write("  " + ",".join("0x%02X" % b for b in bmp[i:i+24]) + ",\n")
        if not bmp: f.write("  0\n")
        f.write("};\n")
        f.write("static const ThGlyph TH_%s_G[] PROGMEM = {\n" % name)
        for (off, w, h, xo, yo, adv) in g:
            f.write("  {%d,%d,%d,%d,%d,%d},\n" % (off, w, h, xo, yo, adv))
        f.write("};\n")
        f.write("static const ThFont TH_%s = {TH_%s_BMP, TH_%s_G, %d, %d};\n\n" % (name, name, name, asc, desc))
print("ok", OUT)
