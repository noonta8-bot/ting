#!/usr/bin/env python3
"""สร้าง src/web_index.h จาก mobile/www/index.html (ฝังหน้าเว็บไว้ในเฟิร์มแวร์ ให้เปิด http://192.168.4.1 ได้)
รัน:  python tools/embed_web.py    (รันซ้ำทุกครั้งที่แก้ index.html)"""
import os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(root, "mobile", "www", "index.html"), encoding="utf-8").read()
if ')NUIHTML"' in src: sys.exit("index.html มี delimiter ชนกัน")
# ESP32 ส่ง PROGMEM ผ่าน send_P : ใช้ raw string ต่อเป็นชิ้น (ข้อจำกัดความยาว literal ของคอมไพเลอร์บางรุ่น)
chunks = [src[i:i + 8000] for i in range(0, len(src.encode("utf-8")) and len(src), 8000)]
out = ["// สร้างอัตโนมัติจาก tools/embed_web.py — อย่าแก้ไฟล์นี้ตรง ๆ", "#pragma once", "#include <pgmspace.h>",
       "static const char INDEX_HTML[] PROGMEM ="]
for c in chunks: out.append('R"NUIHTML(' + c + ')NUIHTML"')
out[-1] += ";"
open(os.path.join(root, "src", "web_index.h"), "w", encoding="utf-8").write("\n".join(out) + "\n")
print("web_index.h", len(src.encode("utf-8")), "bytes")
