#!/usr/bin/env python3
"""ปรับโปรเจกต์ native ที่ `npx cap add android` / `npx cap add ios` สร้างให้ :
   - บังคับแนวนอน  - อนุญาต ws:// (ข้อความไม่เข้ารหัสไปหาเกจ)  - ขอสิทธิ์ GPS / เครือข่ายในวง LAN
รัน:  python3 patch_native.py   (รันซ้ำได้ ไม่ทำซ้ำของเดิม)"""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))

def patch_android():
    p = os.path.join(here, "android", "app", "src", "main", "AndroidManifest.xml")
    if not os.path.exists(p): print("ข้าม Android (ยังไม่มีโฟลเดอร์ android)"); return
    s = open(p, encoding="utf-8").read(); o = s
    for perm in ("ACCESS_FINE_LOCATION", "ACCESS_COARSE_LOCATION", "ACCESS_NETWORK_STATE", "CHANGE_NETWORK_STATE", "ACCESS_WIFI_STATE", "WAKE_LOCK"):
        tag = '<uses-permission android:name="android.permission.%s" />' % perm
        if tag not in s: s = s.replace("</manifest>", "    %s\n</manifest>" % tag)
    if "android:usesCleartextTraffic" not in s: s = s.replace("<application", '<application android:usesCleartextTraffic="true"', 1)
    if "android:screenOrientation" not in s: s = s.replace("<activity", '<activity android:screenOrientation="sensorLandscape"', 1)
    if s != o: open(p, "w", encoding="utf-8").write(s)
    print("Android: ok")

def patch_ios():
    p = os.path.join(here, "ios", "App", "App", "Info.plist")
    if not os.path.exists(p): print("ข้าม iOS (ยังไม่มีโฟลเดอร์ ios)"); return
    s = open(p, encoding="utf-8").read(); o = s
    def add(key, xml):
        nonlocal s
        if "<key>%s</key>" % key not in s: s = s.replace("</dict>\n</plist>", "\t<key>%s</key>\n\t%s\n</dict>\n</plist>" % (key, xml))
    add("NSLocationWhenInUseUsageDescription", "<string>ใช้ GPS ของมือถือวัดความเร็วส่งให้เกจ</string>")
    add("NSLocalNetworkUsageDescription", "<string>เชื่อมต่อกับเกจ NUI ผ่าน Wi-Fi</string>")
    add("NSAppTransportSecurity", "<dict><key>NSAllowsArbitraryLoads</key><true/><key>NSAllowsLocalNetworking</key><true/></dict>")
    add("UIRequiresFullScreen", "<true/>")
    land = "<array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>"
    s = re.sub(r"(<key>UISupportedInterfaceOrientations(?:~ipad)?</key>\s*)<array>.*?</array>", lambda m: m.group(1) + land, s, flags=re.S)
    if s != o: open(p, "w", encoding="utf-8").write(s)
    print("iOS: ok")

patch_android(); patch_ios()
