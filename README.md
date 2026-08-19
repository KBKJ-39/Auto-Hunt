# Auto-Hunt (rAthena)

ระบบ Auto-Hunt สำหรับ rAthena — ค้นหามอนสเตอร์ เดินไปหา โจมตีอัตโนมัติ ดรอปของ เก็บไอเทม และใช้ยาอัตโนมัติ

## คุณสมบัติ

- ค้นหามอนสเตอร์ในรัศมีที่กำหนด (สูงสุด 150 ช่อง)
- เดินเข้าไปโจมตีอัตโนมัติด้วยสกิลที่กำหนด หรือโจมตีปกติ
- เก็บไอเทมบนพื้นอัตโนมัติ
- ใช้ยาเมื่อ HP ต่ำกว่าค่าที่กำหนด
- หยุดพักเมื่อ HP/SP ไม่พอ
- ใช้ Fly Wing เมื่อติดหรือไม่มีเป้าหมาย
- ตั้งค่าผ่าน NPC หรือคำสั่ง `@autohunt` / `@ah`
- ทำงานร่วมกับ AI Companion ได้

## ไฟล์ในแพ็กเกจ

```
Auto-Hunt/
├── README.md                              # คู่มือนี้
├── README.EN.md                           # English version
├── patches/                               # สำหรับ git patch
│   ├── autohunt-all.patch                 # รวม patch ทั้งหมด
│   └── atcommand.cpp.patch               # patch คำสั่ง @autohunt
├── copy-to-server/                        # (แนะนำ) คัดลอก-วางทับได้เลย
│   ├── conf/
│   │   └── atcommands.yml                # เพิ่ม @autohunt ในคำสั่ง
│   ├── npc/
│   │   ├── scripts_custom.conf           # เพิ่มบรรทัดโหลด NPC
│   │   └── custom/
│   │       └── autohunt_npc.txt          # NPC เมนูตั้งค่า
│   └── src/
│       └── map/
│           ├── autohunt.hpp              # Header file
│           └── autohunt.cpp              # Core implementation
└── npc/
    └── custom/
        └── autohunt_npc.txt              # NPC เมนูตั้งค่า
```

## ข้อกำหนด

- rAthena (master branch) — ทดสอบบน Windows MSVC (Release, Win32)
- ต้องคอมไพล์ใหม่ด้วย MSVC (ไม่ใช่แค่ copy .exe)
- **Important:** Build platform ต้องเป็น **Win32 (x86)** เท่านั้น — x64 จะ error 0xc000007b

## วิธีติดตั้ง

### วิธี A: คัดลอก-วาง (เร็วสุด)

1. คัดลอกทั้งโฟลเดอร์ `copy-to-server/` ไปวางทับที่ root ของเซิร์ฟเวอร์:

   ```
   copy-to-server/conf/atcommands.yml           ->  <server>/conf/atcommands.yml
   copy-to-server/npc/scripts_custom.conf       ->  <server>/npc/scripts_custom.conf
   copy-to-server/npc/custom/autohunt_npc.txt   ->  <server>/npc/custom/autohunt_npc.txt
   copy-to-server/src/map/autohunt.hpp          ->  <server>/src/map/autohunt.hpp
   copy-to-server/src/map/autohunt.cpp          ->  <server>/src/map/autohunt.cpp
   ```

2. ทำตามขั้นตอนต่อไป

### วิธี B: พาทช์โค้ด (merge เข้าเวอร์ชันของคุณ)

```bash
cd <server-root>
git apply patches/autohunt-all.patch
```

### เพิ่มใน Visual Studio Project

เปิด `rAthena.sln` แล้วเพิ่มไฟล์ใน `map-server.vcxproj`:

```xml
<ClCompile Include="autohunt.cpp" />
<ClInclude Include="autohunt.hpp" />
```

เพิ่มใน `map-server.vcxproj.filters`:

```xml
<ClCompile Include="autohunt.cpp">
  <Filter>Source Files</Filter>
</ClCompile>
<ClInclude Include="autohunt.hpp">
  <Filter>Header Files</Filter>
</ClInclude>
```

### เพิ่มใน map.cpp

เปิด `src/map/map.cpp` แล้วเพิ่ม:

**ใน do_init:**
```cpp
do_init_autohunt();
```

**ใน do_final:**
```cpp
do_final_autohunt();
```

**ใน map_quit:**
```cpp
autohunt.logout(sd);
```

### เพิ่มใน atcommand.cpp

เพิ่ม include:
```cpp
#include "autohunt.hpp"
```

เพิ่มฟังก์ชัน ACMD_FUNC(autohunt) ตามไฟล์ `patches/atcommand.cpp.patch`

เพิ่มในตาราง ACMD_DEF:
```cpp
ACMD_DEF(autohunt),
```

### เพิ่มใน conf/groups.yml

ภายใต้ Player group (Id: 0) Commands:
```yaml
      autohunt: true
```

### คอมไพล์ใหม่

```
Configuration: Release
Platform: Win32 (x86) — ห้ามใช้ x64
Build → Build Solution (Ctrl+Shift+B)
```

### Deploy

```
Stop map-server.exe
Copy map-server.exe → build/install/
Start map-server.exe
```

### ทดสอบ

พิมพ์ในเกม:
```
@autohunt status
```

## วิธีใช้งาน

### คำสั่ง Atcommand

| คำสั่ง | ผล |
|---|---|
| `@autohunt` หรือ `@ah` | สตาร์ท/หยุด Auto-Hunt |
| `@autohunt start` | เริ่ม Auto-Hunt |
| `@autohunt stop` | หยุด Auto-Hunt |
| `@autohunt status` | แสดงสถานะปัจจุบัน |
| `@autohunt config` | แสดง config ปัจจุบัน |
| `@autohunt config skill auto` | ตรวจสกิลจาก Hotbar อัตโนมัติ |
| `@autohunt config skill 0` | โจมตีปกติเท่านั้น |
| `@autohunt config skill <id> <lv>` | ใช้สกิลที่กำหนด |
| `@autohunt config hp <1-100>` | ค่า HP หยุด (%) |
| `@autohunt config sp <1-100>` | ค่า SP หยุด (%) |
| `@autohunt config range <1-150>` | รัศมีค้นหา (ช่อง) |
| `@autohunt config potion <id\|0>` | ใช้ยาอัตโนมัติ (0=ปิด) |

### NPC Menu

เดินไปคุยกับ NPC "Auto-Hunt Agent" ที่ Prontera (155,180)

- Start / Stop / Status
- Configuration: Skill, HP, SP, Range, Potion

### State Machine

```
IDLE → SCANNING → MOVING → ATTACKING → LOOTING → SCANNING
                ↗ TELEPORTING (เมื่อติด)
                ↘ PAUSED (เมื่อ HP/SP ต่ำ)
```

## Default Settings

| Setting | Default | Max |
|---|---|---|
| Search Range | 30 cells | 150 |
| HP Threshold | 30% | 100% |
| SP Threshold | 10% | 100% |
| Skill | Normal attack | — |
| Auto-Potion | Disabled | — |
| Loot Range | 15 cells | — |
| Timer Interval | 500ms | — |

## แก้ไขปัญหา

- **"Unknown command"** — restart เซิร์ฟเวอร์ (atcommands.yml โหลดตอน start เท่านั้น)
- **ไม่เดิน** — เช็คว่าผู้เล่นนั่งอยู่ (/sit) หรือไม่, เช็ค canmove_tick ใน console
- **เก็บของไม่ได้** — ต้องเดินไปช่องที่มีไอเทม, บางช่องเดินไม่ได้
- **error 0xc000007b** — Build ผิด platform, ต้องเป็น Win32 ไม่ใช่ x64
- **เดินติดหิน** — Auto-Hunt จะ rescan หาเป้าหมายใหม่หลังจากติด 3 วินาที

## ผู้คิดไอเดีย / ผู้พัฒนา

- **ผู้คิดไอเดียระบบ:** KBKJ
- **ผู้พัฒนา / เขียนโค้ด:** AI Opencode — Model Big Pickle

---

## License

ฟรีสำหรับใช้งานส่วนตัวและเชิงพาณิชย์ ภายใต้ [rAthena License](https://github.com/rathena/rathena/blob/master/COPYING)
