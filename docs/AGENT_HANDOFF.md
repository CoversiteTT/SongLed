# Agent Handoff Notes

This document captures the current project state for future agents.
It includes key file locations, behavior notes, and a focused record of "花屏"
troubleshooting attempts and mitigations.

## Project snapshot
- Board: ESP32-S3 N16R8
- Display: ST7789 320x240 (4-wire SPI)
- Encoder: EC11 (A/B + push), extra K0 button (back)
- Firmware: ESP-IDF via PlatformIO
- UI: third_party/oled-ui-astra
- PC helper (current primary): C# tray app in `pc/SongLedPc`
- PC helper (legacy): C++ tray app in `pc/SongLedPcCpp`

## Wiring (default pins)
TFT:
- SCL -> GPIO12
- SDA -> GPIO11
- RES -> GPIO7
- DC  -> GPIO9
- CS  -> GPIO10
- BLK -> GPIO14

Encoder + buttons:
- A -> GPIO15
- B -> GPIO16
- PUSH -> GPIO17
- K0 -> GPIO18

## Key firmware files
- `src/main.cpp` (menus, serial protocol, settings, NVS, test mode)
- `src/hal_astra_esp32.cpp` / `.h` (display driver, encoder, render pipeline)
- `third_party/oled-ui-astra/Core/Src/astra/config/config.h` (UI config)
- `third_party/oled-ui-astra/Core/Src/astra/ui/item/menu/menu.cpp` (list rendering + scrolling)

## Serial protocol
ESP -> PC:
- `HELLO`
- `VOL GET`
- `VOL SET <0-100>`
- `MUTE`
- `SPK LIST`
- `SPK SET <index>`

PC -> ESP:
- `HELLO OK`
- `VOL <0-100>`
- `MUTE <0/1>`
- `SPK BEGIN`
- `SPK ITEM <index> <name>`
- `SPK END`
- `SPK CUR <index>`

Now Playing / Lyrics (PC -> ESP):
- `NP META <title>\t<artist>` (tab separator; MCU also accepts `|` as fallback)
- `NP PROG <pos_ms> <dur_ms>`
- `NP CLR`
- `NP COV BEGIN 40 40`
- `NP COV DATA <hex>` (RGB565 hex, 100 pixels per line)
- `NP COV END`
- `LRC CUR <text>`
- `LRC NXT <text>`
- `LRC CLR`

## Handshake / sync behavior
MCU:
- Sends `HELLO` every second until `HELLO OK` received.
- After handshake, triggers `VOL GET` + `SPK LIST` once.

PC (C++):
- Opens port, then immediately sends `HELLO`.
- On receiving `HELLO`, replies `HELLO OK` and pushes current volume/mute + speaker list.
- Serial read thread enqueues lines; main thread processes (COM safety fix).

PC (C#):
- `SerialWorker` emits `HelloReceived` after `HELLO OK`.
- `SmtcBridge.ResendNowPlaying()` pushes latest META/PROG/COVER on handshake
  (fixes "connect mid-play shows only lyrics").

## Settings (NVS)
Stored under namespace `songled`:
- `ui_speed` (1..50)
- `sel_speed` (1..50)
- `wrap_pause` (0..50)
- `font_hue` (0..50)
- `scroll_ms` (1..50)  => maps to 100..5000 ms

Safety reset:
- Boot + hold K0 within 0.5s for 5s -> erase settings.

## UI behavior changes
- List long text scroll: single-direction loop, 3s per full cycle by default.
  Uses `astra::getUIConfig().listScrollLoopMs`.
- Font color adjustable (HSV hue mapped to RGB565).
- Adjust screen: circular arc progress.

## Fonts (future lyrics module)
- Target font: zpix pixel font release
  - Source: https://github.com/SolidZORO/zpix-pixel-font/releases
- Current local file (not wired into build yet):
  - `third_party/oled-ui-astra/Other/bdfconv/u8g2_font_zpix.c`
- To enable later:
  1) Move/copy to `third_party/oled-ui-astra/Core/Src/hal/hal_dreamCore/components/oled/graph_lib/u8g2/`
  2) Define `U8G2_USE_LARGE_FONTS`
  3) Switch font pointer in `third_party/oled-ui-astra/Core/Src/astra/config/config.h`
     to `u8g2_font_zpix`

## Encoder tuning (slow turn sensitivity)
Location: `HALAstraESP32::readEncoderSteps()`
Current parameters:
- `stepsPerDetent = 4`
- `partialThreshold = 2`
- `partialTimeoutMs = 60`
- `syntheticCooldownMs = 120`
Rationale: detect slow turns, but avoid double-trigger on partial + full step.

## PC helper (primary)
### C# app (current)
Paths:
- `pc/SongLedPc/Program.cs` (tray app + wiring)
- `pc/SongLedPc/SmtcBridge.cs` (SMTC, lyrics, cover + now playing protocol)
- `pc/SongLedPc/SerialWorker.cs` (HELLO, line IO, handshake event)

Notes:
- Uses SMTC (`GlobalSystemMediaTransportControlsSessionManager`).
- Filters sessions by `Genres` containing `NCM-<id>` (NetEase desktop).
- Lyrics fetch: `https://music.163.com/api/song/lyric?id=<id>&lv=1&kv=1&tv=-1`
- Cover: WinRT decode -> 40x40 -> RGB565 -> `NP COV` stream.
- Logs rotate: 512KB max, keep last 5 (`songled-*.log`).
- NuGet fix: `Microsoft.Windows.SDK.NET` pinned to `10.0.18362.6-preview`
  due to unavailable newer versions.

### C++ app (legacy)
Path: `pc/SongLedPcCpp/main.cpp`
Key fixes:
- Disable DTR/RTS to avoid ESP reset on COM open.
- COM operations done on main thread via queue (`WM_SERIAL`).
- Added VC++ runtime check with prompt to download.
- Added open failure diagnostics (MessageBox with error text).

Build:
- Requires MSVC Build Tools.
- A portable CMake is unpacked to `tools/cmake/.../cmake.exe`.
- Build command (example):
  - `VsDevCmd.bat -arch=amd64`
  - `cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build -G "NMake Makefiles"`
  - `cmake --build pc/SongLedPcCpp/build --config Release`
- Output:
  - `pc/SongLedPcCpp/build/SongLedPcCpp.exe`
  - copy to `pc/SongLedPcCpp.exe`

## "花屏" troubleshooting record (keep in mind)
This issue appeared multiple times while changing UI + performance tweaks.
Key lessons and mitigations:
1) **Start from known-good minimal driver**
   - A clean "bringup" project helped isolate wiring vs software.
2) **SPI frequency matters**
   - 80 MHz is stable for this setup.
   - >80 MHz or unverified DMA settings caused instability.
3) **Avoid partial-draw corruption**
   - Partial refresh, split dirty regions, or incorrect line buffer handling
     often caused tearing/flicker. Full redraw stabilizes debugging.
4) **DMA + frame buffer changes**
   - Switching DMA/framebuffer settings mid-run can break state.
   - Use `applyDisplayConfig()` to reinit safely (with mutex).
5) **Color/rotation config**
   - Wrong color order or rotation can look like “noise”.
   - Ensure ST7789 init uses correct MADCTL / color order.
6) **Use known stable config when debugging**
   - Disable DMA or framebuffer when isolating issues.
7) **Menu changes causing '花屏'**
   - Often linked to rendering changes and incorrect scaling/dirty-rect logic.
   - Revert to full redraw to test; then optimize again.

If "花屏" returns:
- First: reduce SPI to 80 MHz, disable DMA, disable framebuffer.
- Then: verify init sequence and color order.
- Then: revert to full-screen redraw for a few frames.

## Current known issue (Jan 28)
User reports:
- If connecting mid-play, only lyrics show; cover missing; title "Unknown";
  progress bar not updating.

Mitigation already added (check if present):
- `SerialWorker.HelloReceived` triggers `SmtcBridge.ResendNowPlaying()`.
- `NP META` clears cover state to avoid stale image.

If still failing:
- Check `pc/SongLedPc/bin/Release/net8.0-windows/songled.log` for:
  - `SMTC track: ...`
  - `Lyric loaded: ...`
  - `SMTC cover skipped: thumbnail null`
- If thumbnail null, add fallback to fetch cover from NetEase API by NCM id.
- On MCU, add debug log on `NP META/NP PROG/NP COV` parsing to confirm receive.
