# SongLed - ESP32-S3 Volume Knob for Windows 11

**Languages**: [English](#english-overview) | [日本語](#日本語概要) | [中文](#中文介绍)  
**Documentation**: [Full Multilingual](README_MULTILINGUAL.md) | [Guide](DOCUMENTATION_GUIDE.md)

---

## English Overview

A feature-rich desktop volume control peripheral based on ESP32-S3 + ST7789 2.4" TFT + EC11 rotary encoder. Communicates with Windows 11 via USB serial to control volume, switch audio devices, display lyrics, and show album artwork.

**Quick Links**: [Multilingual Docs](README_MULTILINGUAL.md) | [Hardware Setup](#wiring-diagram) | [Build Guide](#firmware-build) | [License](LICENSE)

---

## 日本語概要

ESP32-S3 + ST7789 2.4" TFT + EC11ロータリーエンコーダベースの機能豊富なデスクトップボリュームコントロール。Windows 11とUSBシリアル接続でボリューム制御、オーディオデバイス切り替え、リリック表示、アルバムアートワーク表示が可能です。

**クイックリンク**: [多言語ドキュメント](README_MULTILINGUAL.md) | [ハードウェアセットアップ](#ハードウェア配線) | [ビルドガイド](#ファームウェアビルド) | [ライセンス](LICENSE)

---

## 中文介绍

一个基于 ESP32-S3 + ST7789 2.4" TFT + EC11 旋钮的桌面音量外设项目。通过 USB 串口与 Win11 通信，实现音量控制、音频输出切换、歌词显示和专辑封面显示。

**快速链接**: [多语言文档](README_MULTILINGUAL.md) | [硬件设置](#硬件接线) | [构建指南](#固件编译) | [许可证](LICENSE)

---

## 📋 仓库介绍
一个基于 ESP32-S3 + ST7789 2.4\" TFT + EC11 旋钮的桌面音量外设项目。通过 USB 串口与 Win11 PC 端程序通信，实现音量/输出设备控制、菜单 UI、歌词与专辑封面浮窗显示。固件使用 ESP-IDF + PlatformIO，PC 端以 C# 托盘程序为主。

## 目前待改进（TODO）
- 歌词是否自动滚动的判定逻辑
- 封面加载进度条与传输进度同步
- 歌词延迟优化（通信/刷新策略）
- scrollTime 与 lyricSpeed 详细调整页单位显示
- 浮窗自动关闭时间设置项
- 浮窗关闭后残留清理
- 圆弧调整界面上下限数字重叠

## Hardware identified from the images
- 2.4" TFT, 320x240, ST7789, 4-wire SPI
- EC11 rotary encoder (A/B + push)
- Extra button labeled K0 (used as back)
- VCC supports 3.3V or 5V
- Dev board: ESP32-S3 N16R8 (native USB available)

## Wiring (default pins in firmware)
TFT:
- GND -> GND
- VCC -> 3V3
- SCL -> GPIO12
- SDA -> GPIO11
- RES -> GPIO7
- DC  -> GPIO9
- CS  -> GPIO10
- BLK -> GPIO14

Encoder + buttons:
- A    -> GPIO15
- B    -> GPIO16
- PUSH -> GPIO17
- K0   -> GPIO18

If you need different pins, update the PIN_ constants in `src/main.cpp`.

## Serial connection to Win11 (ESP-IDF)
默认使用 UART0（板子上标注 COM 的 Type-C 口）。PC 程序就连这个 COM 口即可。
如果你想改为原生 USB-CDC（标注 USB 的 Type-C 口），需要在 ESP-IDF 里开启 USB 控制台/CDC。

The ESP32 sends/receives simple text commands over USB CDC.

## Firmware build (PlatformIO + ESP-IDF)
1) 安装 PlatformIO（VS Code 或 CLI）。
2) 编译：
   ```
   pio run
   ```
3) 烧录：
   ```
   pio run -t upload --upload-port COM6
   ```

## Windows helper (primary: C++)
### C++ helper (recommended)
1) Build (MSVC + CMake):
   ```
   cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
   cmake --build pc/SongLedPcCpp/build --config Release
   ```
   Output EXE: `pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe`
2) Run:
   ```
   pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --port COM6
   ```
   If you have only one serial port, `--port` can be omitted.
   If multiple ports, the helper will try HELLO handshake auto-detection.
3) Auto detect by VID/PID (optional):
   ```
   pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --vid 303A --pid 1001
   ```
4) Quick autostart management (optional):
   ```
   pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --autostart on
   pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --autostart off
   pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --autostart toggle
   ```
5) Runtime:
   - If Microsoft Visual C++ Runtime is missing, the app will prompt and open the download page automatically.

### C# helper (backup)
1) Install .NET 8 SDK (only needed to build).
2) From the repo root (run directly):
   ```
   dotnet run --project pc/SongLedPc -- --port COM6
   ```
   If you have only one serial port, `--port` can be omitted.
   If multiple ports, the helper will try HELLO handshake auto-detection.
3) Auto detect by VID/PID (optional):
   ```
   dotnet run --project pc/SongLedPc -- --vid 303A --pid 1001
   ```
4) Quick autostart management (optional):
   ```
   dotnet run --project pc/SongLedPc -- --autostart on
   dotnet run --project pc/SongLedPc -- --autostart off
   dotnet run --project pc/SongLedPc -- --autostart toggle
   ```
5) Publish single-file EXE:
   ```
   dotnet publish pc/SongLedPc -c Release -r win-x64 /p:PublishSingleFile=true /p:SelfContained=true /p:IncludeNativeLibrariesForSelfExtract=true
   ```
   Output in `pc/SongLedPc/bin/Release/net8.0-windows/win-x64/publish/`

### Tray + autostart
The C# helper runs in the system tray (no console window). Right-click the tray icon to:
- Reconnect
- Toggle startup
- Exit

### Python helper (legacy)
1) Install Python 3.10+.
2) From the repo root:
   ```
   pip install -r pc/requirements.txt
   ```
3) Run:
   ```
   python pc/win_audio_bridge.py --port COM6
   ```
   If you have only one serial port, `--port` can be omitted. Multiple ports will try HELLO handshake auto-detection.

## UI controls
- Rotate encoder: move selection or change volume on the Volume screen.
- Press encoder: confirm / enter.
- K0 button: back.

## Fonts (future lyrics)
- Planned font: zpix pixel font (release)
  - Source: https://github.com/SolidZORO/zpix-pixel-font/releases
- Local file currently parked (not in build yet):
  - `third_party/oled-ui-astra/Other/bdfconv/u8g2_font_zpix.c`

## Serial protocol (ESP32 <-> PC)
From ESP32:
- `VOL GET`
- `VOL SET <0-100>`
- `MUTE`
- `SPK LIST`
- `SPK SET <index>`
- `HELLO` (handshake request)

From PC:
- `VOL <0-100>`
- `MUTE <0/1>`
- `SPK BEGIN`
- `SPK ITEM <index> <name>`
- `SPK END`
- `SPK CUR <index>`
- `HELLO OK` (handshake response)

## Extending menus
Menu items are defined in `src/main.cpp`:
- Add new items to `mainItems`.
- Add new actions by creating new `actionX` functions and assigning them to items.

## Project layout
- `src/` Firmware sources (ESP-IDF)
- `third_party/` UI framework
- `pc/` PC helpers (C# + Python + C++ version)
- `experiments/` Bring-up / scratch projects
- `docs/images/` Hardware photos

## Handoff & troubleshooting
- `docs/HANDOFF.md` (交接稿 + 关键注意事项)
- `RELEASE_CHECKLIST.md` (GitHub 发布前检查清单)

## Third-party libraries and attributions

This project incorporates and modifies the following third-party libraries:

### 1. **oled-ui-astra** (UI Framework)
- **Source**: https://github.com/dcfsswindy/oled-ui-astra
- **License**: GNU General Public License v3.0 (GPLv3)
- **Location**: `third_party/oled-ui-astra/`
- **Usage**: Core UI framework adapted for ESP32-S3 with 2.4" TFT display
- **Modifications**: Hardware abstraction layer, display integration, menu customization

### 2. **U8G2** (Graphics Library)
- **Source**: https://github.com/olikraus/u8g2
- **License**: BSD 3-Clause License
- **Location**: `third_party/oled-ui-astra/Core/Src/hal/hal_dreamCore/components/oled/graph_lib/u8g2/`
- **Usage**: Graphics rendering, font support, display buffering
- **Note**: Integrated as part of the oled-ui-astra framework

### 3. **ZPIX Pixel Font**
- **Source**: https://github.com/SolidZORO/zpix-pixel-font
- **License**: OFL (Open Font License) 1.1
- **Location**: Font data included in `u8g2_font_zpix.c`
- **Usage**: Planned for lyrics display (currently not active in build)

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)** to comply with the oled-ui-astra framework license.

See [LICENSE](LICENSE) file for details.

## PC Dependencies

### C# version (SongLedPc)
- NAudio 2.2.1 - Audio control (SMTC bridge)
- Microsoft.Windows.SDK.NET - Windows API interop
- System.Management - Device management
- System.IO.Ports - Serial communication

### C++ version (SongLedPcCpp)
- Win32 API (ole32, oleaut32, uuid, setupapi, shell32, shlwapi)
- System serial port communication (no external dependencies)

### Python version (legacy)
- PySerial - Serial communication

All rights to third-party libraries are retained by their respective authors.

---

## Documentation

- [Full Multilingual README](README_MULTILINGUAL.md) - Complete docs in English, �ձ��Z, ����
- [Documentation Guide](DOCUMENTATION_GUIDE.md) - Language selection
- [GitHub Discussions](https://github.com/CoversiteTT/SongLed/discussions) - Questions and help
