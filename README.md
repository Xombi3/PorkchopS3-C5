PorkChop CYD — NM-CYD-C5 Port Setup
Original project: https://github.com/0ct0sec/M5PORKCHOP by 0ct0sec
Port for NM-CYD-C5

1. Arduino IDE Setup
Board: ESP32C5 Dev Module
Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
Install the esp32 board package (Espressif) if not already present
2. File Placement
Sketch folder
Put `PorkChop_CYD_C5.ino` in its own folder (Arduino requires the folder name
to match the .ino filename), e.g.:
```
Documents/Arduino/PorkChop_CYD_C5/PorkChop_CYD_C5.ino
```
TFT_eSPI library
I patched TFT_eSPI to support the ESP32-C5 processor. Copy these into your
TFT_eSPI library folder, overwriting the existing files:
```
Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
Documents/Arduino/libraries/TFT_eSPI/TFT_eSPI.h
Documents/Arduino/libraries/TFT_eSPI/TFT_eSPI.cpp
```
And add the new processor files:
```
Documents/Arduino/libraries/TFT_eSPI/Processors/TFT_eSPI_ESP32_C5.h
Documents/Arduino/libraries/TFT_eSPI/Processors/TFT_eSPI_ESP32_C5.c
```
If you don't already have TFT_eSPI installed, install it first via Library
Manager, then overwrite/add the files above.
3. Confirmed Pin Map (NM-CYD-C5)
Function	GPIO	Notes
TFT MOSI	7	
TFT SCLK	6	Shared bus with touch
TFT MISO	2	Commented out — ST7789 doesn't need it
TFT CS	23	
TFT DC	24	
TFT BL	25	Active HIGH
Touch CS	1	
Touch MISO	9	I found this by GPIO scan — the published pinout (GPIO2) is wrong
Touch CLK	6	Shared with TFT
Touch MOSI	7	Shared with TFT
SD CS	10	
GPS RX	4	
GPS TX	5	
Important: GPIO 10–15 on ESP32-C5 are internal flash SPI pins. Never
configure these as general-purpose outputs — it can corrupt flash access.
4. Touch Status
Touch is currently disabled in the sketch (`getPoint()` returns `false`
unconditionally). I ran into the bit-banged touch implementation corrupting
TFT_eSPI's display state whenever `SPI.begin()` was called to restore the
shared CLK/MOSI lines. I haven't found a clean fix yet — the next thing to
try is giving touch its own dedicated SPI peripheral (e.g. `SPI2_HOST`) so
it never has to fight TFT_eSPI for the bus.
5. SD Card
Uses `SPI2_HOST` with CS on GPIO10. If SD detection fails:
Confirm the card is FAT32 formatted
Confirm the card is seated correctly
Note that SD and touch currently can't reliably coexist on the shared bus
until the touch SPI separation above is resolved
6. GPS
ATGM336H module on `Serial2`, RX=GPIO4, TX=GPIO5, 9600 baud. No special
setup needed beyond correct wiring.
7. ESP-NOW / PigSync (Flock Detection)
If flock/BOAR BROS discovery isn't finding peers, the most common cause I've
seen is the WiFi mode. ESP-NOW broadcast packets are unreliable to receive
in pure `WIFI_STA` mode — the STA interface can silently drop unassociated
broadcast frames. I switched both `PigSync::init()` and `PigSync::start()`
to use `WIFI_AP_STA` instead of `WIFI_STA`, which fixed discovery in my
testing.
Discovery runs on a fixed channel (`PIGSYNC_DISCOVERY_CHANNEL = 1`). Both
devices need to be on this channel during the discovery phase. I added debug
logging to `_sendDiscover()` — watch the serial monitor for:
```
[PIGSYNC] broadcasting CMD_DISCOVER ch=1 mac=XX:XX:XX:XX:XX:XX
```
If you see this on both devices but no `[PIGSYNC] Beacon from ...` response,
double check both boards are actually scanning/discoverable at the same time
and not on different channels.
8. Known Working / Not Working Summary
Working:
Display rendering
WiFi network scanning
Boot sequence / splash screen
SPIFFS
ESP-NOW / PigSync discovery (after the WIFI_AP_STA fix)
Not working / in progress:
Touch input (disabled — SPI bus contention with display)
SD card (likely blocked by the same shared-bus issue)
RGB LED (pins not yet identified for this board)
9. Flashing
If you need to fully erase flash before a fresh upload:
```
esptool.exe --port COMxx erase_flash
```
(Adjust the esptool path to match your Arduino15 packages install location
and your actual COM port.)
Documents\Arduino\
 How files should be organized to get it working properly
  PorkChop_CYD_C5\
    PorkChop_CYD_C5.ino

  libraries\TFT_eSPI\
    User_Setup.h          ← User_Setup_C5.h renamed
    User_Setup_Select.h
    TFT_eSPI.h
    TFT_eSPI.cpp
    Processors\
    TFT_eSPI_ESP32_C5.c

S3 known working stuff summarized 


PorkChop S3 — Hosyond ESP32-S3 2.8" Port
A full port of PorkChop CYD
(WiFi security tool) from the ESP32 CYD ("Cheap Yellow Display") board to the
Hosyond ESP32-S3 2.8" IPS display (SKU ES3C28P). This document covers
the hardware pin mapping, the Arduino IDE settings that actually work on this
board, every workaround I had to build to get a stable boot, and the full
feature set as it stands now — including real `.pcap` capture to SD.
I'm writing this from my own perspective as the person who built and debugged
this, in case it's useful to revisit later or hand to someone else picking up
the project.
---
1. Confirmed working hardware
Component	Chip	Interface
Display	ILI9341V	SPI
Touch	FT6336G	I²C (capacitive)
SD card	—	SDIO 4-bit
RGB LED	WS2812B (NeoPixel)	single-wire
Speaker	—	PWM (LEDC)
MCU	ESP32-S3 (QFN56, silicon rev v0.2)	—
Flash	4 MB	DIO mode
PSRAM	8 MB embedded, unusable in this build	OPI (disabled)
USB	USB-Serial/JTAG (no external CH340/CP2102)	—
Pinout
Function	Pin	Notes
TFT CS	GPIO 10	
TFT DC	GPIO 46	⚠️ strapping pin — see §3.1
TFT MOSI	GPIO 11	
TFT SCLK	GPIO 12	
TFT MISO	GPIO 13	unused (write-only display)
TFT Backlight	GPIO 45	active HIGH
Touch SDA	GPIO 16	
Touch SCL	GPIO 15	
Touch RST	GPIO 18	
Touch INT	GPIO 17	not used — I poll instead
Touch I²C addr	`0x38`	FT6336G
Speaker	GPIO 21	LEDC PWM, never use GPIO 6 (see §3.2)
NeoPixel (RGB LED)	GPIO 42	single WS2812B, `NEO\\\\\\\_GRB + NEO\\\\\\\_KHZ800`
SD CLK	GPIO 38	
SD CMD	GPIO 40	
SD D0	GPIO 39	
SD D1	GPIO 41	4-bit mode only
SD D2	GPIO 48	4-bit mode only
SD D3	GPIO 47	4-bit mode only
---
2. Arduino IDE board settings (confirmed working)
```
Board:              ESP32S3 Dev Module
Flash Mode:         DIO
Flash Frequency:    80 MHz
Flash Size:         4 MB (32Mb)
Partition Scheme:   Huge APP (3MB No OTA/1MB SPIFFS)
PSRAM:              Disabled
CPU Frequency:      240 MHz
USB Mode:           Hardware CDC and JTAG
Upload Speed:       921600
Erase All Flash:    Enabled  (only needed once — see §3.5)
```
Library required: Adafruit NeoPixel (Tools → Manage Libraries → search
"Adafruit NeoPixel" → Install). Everything else — `SPI`, `Wire`, `WiFi`,
`esp\\\\\\\_wifi`, `Preferences`, `SD\\\\\\\_MMC` — is built into the ESP32 Arduino core.
After every upload, press the physical RESET button on the board. This
board's USB-Serial/JTAG interface does not reliably auto-reset after
flashing — the chip sits at the `ESP-ROM:esp32s3-20210327` bootloader banner
until RESET is pressed. This is normal, not a fault.
---
3. Root causes & fixes (the part that actually matters)
Everything below was diagnosed by elimination: I used a bare-metal
diagnostic sketch (no WiFi, no libraries, raw SPI colour fills in a loop) as
a known-good baseline. If the diagnostic stayed on screen and the full
sketch didn't, the bug was in my code, not the hardware — that distinction
drove every fix here.
3.1 TFT_eSPI crashes on this chip — abandoned entirely
Symptom: `Guru Meditation Error: Core 0 panic'ed (StoreProhibited)`,
`EXCVADDR: 0x00000010`, looping reboot.
Cause: `TFT\\\\\\\_eSPI tft = TFT\\\\\\\_eSPI();` declared at global scope runs its
constructor during C++ static initialization, before `setup()` runs and
before the SPI peripheral is mapped on this ESP32-S3 revision. The
constructor dereferences something through an unmapped pointer → null +
16-byte offset (`0x10`) → crash. Moving it to heap allocation
(`new TFT\\\\\\\_eSPI()`) inside `setup()` didn't help — it still crashed during
`tft.init()`.
Fix: I removed TFT_eSPI completely. The display is driven by a
hand-written bare-metal SPI driver (`dispInit()`, `dwin()`, raw
`SPIClass(FSPI)` calls). No `User\\\\\\\_Setup.h` is needed at all in the current
sketch.
3.2 GPIO 6 is the TFT MOSI line — never use it for anything else
Driving GPIO 6 with `ledcAttach()` for the speaker silently kills the SPI
bus (black screen, then crash loop), because GPIO 6 is shared with TFT MOSI
on this board's wiring. I moved the speaker to GPIO 21, a free pin.
3.3 Display "fades to black" after ~2–3 seconds — needs continuous GRAM writes
Symptom: Splash/content appears correctly, then smoothly fades to black
over about a second, backlight stays on throughout. Reproducible every
boot, always at the same elapsed time.
This is not a crash, not a sleep command, and not a power/backlight
fault — I ruled out all of those individually (confirmed via a diagnostic
sketch that held a static colour fine without any updates). The actual
cause: the ILI9341V needs to keep receiving GRAM (frame-buffer) writes. My
original code only pushed a frame when content changed (`doRedraw` flag), so
during any idle period (e.g. the screen sat static for >2s) the panel's
pixel data decayed.
Fix:
```cpp
if(doRedraw) drawAll(mode);   // rebuild framebuffer content when needed
fbFlush();                    // ALWAYS push the full frame, every loop, unconditionally
```
`fbFlush()` also re-sends a `DISPON` (`0x29`) command after every push as a
defensive resync, and runs at ~60 FPS (16ms loop delay) regardless of
whether anything changed. This single change is what got the display to
"just work."
3.4 `esp\\\\\\\_wifi\\\\\\\_80211\\\\\\\_tx` crashes when a scan is in-flight
Symptom: Sporadic crash/reboot in OINK or BACON mode, especially right
around the periodic background scan.
Cause: Raw frame injection (`esp\\\\\\\_wifi\\\\\\\_80211\\\\\\\_tx`, used for deauth and
fake beacon frames) is illegal while `WiFi.scanNetworks()` is active — the
ESP32-S3 WiFi driver panics if you try to inject while it's mid-scan.
Fix: all injection is gated:
```cpp
if(mode==M::OINK \\\\\\\&\\\\\\\& !scanning \\\\\\\&\\\\\\\& WiFi.scanComplete()!=WIFI\\\\\\\_SCAN\\\\\\\_RUNNING){ ... }
```
and `esp\\\\\\\_wifi\\\\\\\_set\\\\\\\_channel()`'s return value is checked before injecting —
if the driver is busy, the inject is skipped for that loop rather than
forced.
3.5 `ESP-ROM:esp32s3-20210327` with nothing after it — corrupted bootloader
If you change Flash Mode / Frequency / Partition / PSRAM settings between
uploads without erasing first, the bootloader Arduino just wrote can
mismatch the settings the chip now expects, and it hangs at the ROM banner
forever.
Fix: `Tools → Erase All Flash Before Sketch Upload → Enabled`, upload
once, then you can turn it back off. If Arduino IDE's erase option
misbehaves, do it manually with the bundled esptool:
```
<arduino-ide-data>\\\\\\\\packages\\\\\\\\esp32\\\\\\\\tools\\\\\\\\esptool\\\\\\\_py\\\\\\\\<version>\\\\\\\\esptool.exe ^
  --chip esp32s3 --port COM5 --baud 460800 erase-flash
```
3.6 Browser caches stale sketch downloads
Several "fixes" appeared not to apply because the browser served a cached
copy of a previously downloaded `.ino` file with the same name. If a compile
error references code that's provably already fixed in the source, don't
re-download — open a new sketch in Arduino IDE, select-all, delete, and
paste the file contents directly instead.
3.7 Promiscuous mode gets silently disabled by deauth injection
Symptom: A pcap capture would just stop accumulating packets partway
through an OINK session, with no error.
Cause: the deauth injection code disables promiscuous mode before
switching channels (required to avoid a driver conflict — see §3.4), but
never turned it back on, so if a capture was running it died silently the
first time a deauth burst fired.
Fix:
```cpp
bool wasCapturing = pcapCapturing;
esp\\\\\\\_wifi\\\\\\\_set\\\\\\\_promiscuous(false);
// ... channel switch + inject ...
if(wasCapturing){
    esp\\\\\\\_wifi\\\\\\\_set\\\\\\\_promiscuous\\\\\\\_rx\\\\\\\_cb(pcapPromiscCb);
    esp\\\\\\\_wifi\\\\\\\_set\\\\\\\_promiscuous(true);
}
```
---
4. Software architecture
```
┌─────────────────────────────────────────┐
│ setup()                                  │
│  1. LEDC (speaker) attach  — before SPI! │
│  2. WiFi.mode(STA) + settle delay        │
│  3. SD\\\\\\\_MMC init                          │
│  4. Display SPI init (dispInit)          │
│  5. Splash screen, hold \\\\\\\~1.5s            │
│  6. Touch (FT6336G) init                 │
│  7. NeoPixel init                        │
│  8. Load prefs (theme, sound, XP, level) │
│  9. avInit() / specInit()                │
│ 10. First WiFi scan kicked off           │
└─────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────┐
│ loop()  — everything happens here        │
│  • pcapDrainRing() — flush ISR packets   │
│  • checkScan() — poll async scan result  │
│  • avatar animation state machine        │
│  • spectrum / waterfall buffer update    │
│  • NeoPixel mode-colour logic            │
│  • touch poll → tap/hold event           │
│  • mode-specific logic (inject, log…)    │
│  • if(doRedraw) drawAll(mode)            │
│  • fbFlush()   ← always, unconditional   │
└─────────────────────────────────────────┘
```
Why a software framebuffer?
`FB\\\\\\\[320\\\\\\\*240]` (153,600 bytes, RGB565) lives in SRAM. All drawing happens
into this buffer; the entire screen is pushed to the panel in one
`dspi.writeBytes()` burst per frame. This avoids partial/torn screen updates
and is what makes §3.3's "always flush" fix possible without visible
tearing.
---
5. Feature breakdown/Avatar system
Ported directly from the original CYD source: 7 mood states (NEUTRAL,
HAPPY, EXCITED, HUNTING, SLEEPY, SAD, ANGRY) as exact ASCII frame arrays
(`FRAMES\\\\\\\_R` / `FRAMES\\\\\\\_L` for facing right/left), with:
Smooth slide animation between two screen positions, eased with a
smoothstep curve
Random blink, sniff (nostril animation), and idle look-around behaviour
A scrolling, self-mutating "grass" strip (`/\\\\\\\\/\\\\\\\\/\\\\\\\\` pattern) that moves
while the pig is "walking" — active in OINK, DNH, WARHOG, and PORK PATROL
5.2 Spectrum analyzer
Not a simple per-channel bar chart — it's a proper simulated RF view:
Each detected network contributes a sinc² lobe (~22MHz wide) at its
channel's centre frequency, so overlapping channels visibly interfere
like real WiFi does
Peak-hold with gradual decay
A 40-row scrolling waterfall display below the live trace, dithered
by signal intensity
dB-labelled Y-axis and channel-numbered X-axis
5.3 LED status indicator (WS2812B on GPIO 42)
Mode	Behaviour
OINK	Green, slow pulse (500ms)
WARHOG	Purple, slow pulse (600ms)
BACON	Solid red
PORK PATROL	Red/blue siren (200ms alternating)
Everything else	Off
5.4 Pork Patrol
Continuously scans visible SSIDs against known Flock Safety and Axon/police
bodycam naming patterns, while the pig enters its HUNTING animation state
and walks through grass. Shows a live "SCANNING..." or a red `!! ALERT !!`
banner with counts the moment a match is found. Runs the red/blue siren LED
the whole time it's active.
5.5 SD card logging
```
/captures/   — raw .pcap files (see §5.6)
/wigle/      — WiGLE-format CSV exports from wardriving
/logs/       — porkchop.log, plain-text event log
```
Card must be FAT32. If no card is present, every SD-related feature is
silently skipped (`sdOk == false` guards everything) — nothing else is
affected. Init tries 4-bit SDIO first, falls back to 1-bit automatically.
5.6 Real pcap capture — the main addition in this pass
OINK MODE and DO NO HAM now write genuine, standard-format `.pcap` files
to SD that open directly in Wireshark, aircrack-ng, or `hcxpcapngtool` —
no conversion step needed.
How it's built:
A promiscuous-mode RX callback (`pcapPromiscCb`) runs in WiFi driver ISR
context. It does the absolute minimum: copy the raw frame bytes into a
small ring buffer slot and bump an index. No parsing, no allocation, no SD
access — all of that would be unsafe and far too slow inside an ISR.
Every `loop()` iteration calls `pcapDrainRing()`, which runs on the main
thread, copies frames out of the ring, and writes them to the open SD
file as standard pcap records.
Each file starts with the 24-byte pcap global header
(`magic=0xa1b2c3d4`, `linkType=105` i.e. `LINKTYPE\\\\\\\_IEEE802\\\\\\\_11` — raw
802.11, no radiotap wrapper), followed by one 16-byte record header +
raw frame bytes per packet, with no padding — exactly what the pcap spec
expects.
A lightweight EAPOL detector (`frameLooksLikeEAPOL`) inspects each frame
just to bump an on-screen counter and the handshake stat — it does not
filter what gets written. Every captured frame goes into the file
regardless of type, which is what makes the resulting file a real,
complete capture rather than a pre-filtered EAPOL-only dump.
Capture starts automatically the moment you enter OINK or DNH from the
menu, and stops cleanly (flushed and closed) when you hold to exit either
mode, or if you ever land back in IDLE for any reason (safety net).
Files are named `/captures/oink\\\\\\\_NNNNNN.pcap` or `/captures/dnh\\\\\\\_NNNNNN.pcap`,
where `NNNNNN` is seconds since boot at capture start.
Both screens show live `REC: <packets> pkts <KB>KB` status while capturing.
To use a captured file: copy it off the SD card and either open it
directly in Wireshark, or run it through `hcxpcapngtool` to convert to the
hashcat 22000 format for cracking attempts (only against networks you own
or have explicit authorization to test).
---
6. Known limitations
PSRAM is disabled. This board's 8 MB embedded PSRAM wasn't stable
enough in testing on this configuration to be trusted, so the full-screen
framebuffer lives entirely in regular SRAM instead.
BLE / Piggy Blues mode is stubbed. NimBLE-Arduino integration wasn't
ported in this pass.
ESP-NOW / Boar Bros peer sync is stubbed.
No RTC. pcap timestamps are derived from `millis()` since boot, not
wall-clock time — fine for ordering packets within a session, not
meaningful as an absolute date/time.
---
7. Quick troubleshooting checklist
Symptom	Check
`ESP-ROM:...` and nothing else	Erase All Flash, re-upload (§3.5)
Black screen, backlight on	Press RESET after upload — board doesn't auto-reset
Screen flickers / tears	Confirm `fbFlush()` is unconditional, not behind `if(doRedraw)`
Display fades after a couple seconds	Same as above — it means frames stopped being pushed
Crash in OINK/BACON only	Check injection is gated on `!scanning` (§3.4)
pcap stops growing partway through a session	Check promiscuous mode is restored after injection (§3.7)
`wifi:unsupport frame type: 0c0` in Serial	Harmless — promiscuous mode logging real nearby traffic
SD not detected	FAT32 format, card fully seated, check Serial for which attempt (4-bit/1-bit) failed
Compile error referencing code already fixed	Stale browser-cached `.ino` — paste fresh content instead of re-downloading
