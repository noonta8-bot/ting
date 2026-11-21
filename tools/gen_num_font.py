#!/usr/bin/env python3
"""สร้าง include/num_font.h : ฟอนต์ตัวเลข anti-alias 4 บิต (alpha 16 ระดับ) สำหรับหน้า HOME
ต้นฉบับ: Inter ExtraBold Italic (SIL OFL)  ตัวเลขทุกตัวกว้างเท่ากัน (tabular) ค่าไม่สั่นตอนเปลี่ยน
อักขระ: 0-9 . - : และเว้นวรรค
ใช้:  python3 tools/gen_num_font.py /usr/share/fonts/opentype/inter/Inter-ExtraBoldItalic.otf include/num_font.h
"""
import sys
from PIL import Image, ImageDraw, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
CHARS = "0123456789.-: "
# (ชื่อ, ความสูงฟอนต์ px, สัดส่วนบีบแนวนอน)  บีบให้ตัวสูงแคบแบบเกจรถแข่ง ตามภาพต้นฉบับ
SIZES = [("S", 12, 0.90), ("M", 19, 0.80), ("L", 30, 0.72), ("LL", 38, 0.72), ("XL", 46, 0.70)]
SS = 4                                          # supersample แล้วย่อ -> ขอบเนียนกว่าเรนเดอร์ตรง ๆ


def build(px, xs):
    big = px * SS
    font = ImageFont.truetype(SRC, big, layout_engine=ImageFont.Layout.BASIC)
    adv = {c: font.getlength(c) / SS * xs for c in CHARS}
    dmax = max(adv[c] for c in "0123456789")
    glyphs, bmp = [], bytearray()
    asc = 0
    for c in CHARS:
        a = int(round(dmax if c.isdigit() else adv[c]))
        if c == " ":
            a = max(a, int(round(px * 0.36)))
        W = H = big * 3
        im = Image.new("L", (W, H), 0)
        ox, oy = big, big * 2
        ImageDraw.Draw(im).text((ox, oy), c, font=font, fill=255, anchor="ls")
        im = im.resize((int(W * xs) // SS * SS, H), Image.BILINEAR).crop((0, 0, W, H)) if xs != 1 else im
        ox = int(ox * xs)
        if c.isdigit():                                  # จัดตัวเลขกึ่งกลางช่องกว้างเท่ากัน
            ox_shift = int(round((a - adv[c]) / 2 * SS))
            im = im.transform(im.size, Image.AFFINE, (1, 0, -ox_shift, 0, 1, 0))
        bb = im.getbbox()
        if not bb:
            glyphs.append((len(bmp), 0, 0, 0, 0, a)); continue
        x0 = (bb[0] // SS) * SS; y0 = (bb[1] // SS) * SS
        x1 = -(-bb[2] // SS) * SS; y1 = -(-bb[3] // SS) * SS
        crop = im.crop((x0, y0, x1, y1)).resize(((x1 - x0) // SS, (y1 - y0) // SS), Image.BOX)
        w, h = crop.size
        off = len(bmp)
        px_ = crop.load()
        for yy in range(h):
            row = [min(15, int(round(px_[xx, yy] / 255.0 * 15))) for xx in range(w)]
            if len(row) & 1: row.append(0)
            for i in range(0, len(row), 2):
                bmp.append((row[i] << 4) | row[i + 1])
        xo = (x0 - ox) // SS
        yo = (y0 - oy) // SS
        glyphs.append((off, w, h, xo, yo, a))
        if c.isdigit(): asc = max(asc, -yo)
    return glyphs, bmp, asc


with open(OUT, "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("// ฟอนต์ตัวเลข anti-alias 4bpp (alpha 0-15) สร้างด้วย tools/gen_num_font.py  (ต้นฉบับ: %s)\n" % SRC.split("/")[-1])
    f.write("// อักขระ: \"%s\" = ดัชนี 0..%d ; พิกเซล 2 จุดต่อไบต์ (nibble สูงก่อน) แถวละ (w+1)/2 ไบต์\n\n" % (CHARS, len(CHARS) - 1))
    f.write("struct NfGlyph { uint16_t off; uint8_t w, h; int8_t xo, yo; uint8_t adv; };\n")
    f.write("struct NumFont { const uint8_t* bmp; const NfGlyph* g; uint8_t asc; };\n")
    f.write("static inline int nfIndex(char c) {\n  if (c >= '0' && c <= '9') return c - '0';\n  if (c == '.') return 10;\n  if (c == '-') return 11;\n  if (c == ':') return 12;\n  if (c == ' ') return 13;\n  return -1;\n}\n\n")
    for name, px, xs in SIZES:
        g, bmp, asc = build(px, xs)
        f.write("static const uint8_t NF_%s_BMP[] PROGMEM = {\n" % name)
        for i in range(0, len(bmp), 24):
            f.write("  " + ",".join("0x%02X" % b for b in bmp[i:i + 24]) + ",\n")
        f.write("};\nstatic const NfGlyph NF_%s_G[] PROGMEM = {\n" % name)
        for (off, w, h, xo, yo, adv) in g:
            f.write("  {%d,%d,%d,%d,%d,%d},\n" % (off, w, h, xo, yo, adv))
        f.write("};\nstatic const NumFont NF_%s = {NF_%s_BMP, NF_%s_G, %d};\n\n" % (name, name, name, asc))
        print(name, px, "asc", asc, "bytes", len(bmp), "digit adv", g[0][5])
print("ok", OUT)
