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

## 📋 Project Overview

A desktop volume control peripheral based on ESP32-S3 + ST7789 2.4" TFT + EC11 rotary encoder. Communicates with Windows 11 via USB to control volume, switch audio devices, display lyrics, and show album artwork.

**Current TODOs**:
- Auto scroll logic for lyrics
- Album cover loading progress synchronization
- Lyrics timing optimization
- Detailed adjustment UI for scrollTime and lyricSpeed
- Floating window auto-close timeout setting
- Cleanup after floating window closes
- Arc adjustment interface number overlap fix

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

## Serial Connection

Default: UART0 via Type-C (labeled COM). Connect PC program to this COM port.

Alternative: Enable native USB-CDC on Type-C (labeled USB) by enabling USB console in ESP-IDF configuration.

## Firmware Build

1. Install PlatformIO (VS Code or CLI)
2. Build: `pio run`
3. Upload: `pio run --target upload`

Firmware uses ESP-IDF + PlatformIO. PC companion: C# .NET 8 tray application or C++ Win32 version.

## Windows Helper

### C++ Version (Recommended)

**Build**:
```bash
cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
cmake --build pc/SongLedPcCpp/build --config Release
```

**Run**:
```bash
pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --port COM6
```

Options:
- `--port COM6` - Specify serial port (auto-detect if omitted)
- `--vid 303A --pid 1001` - USB device ID auto-detection
- `--autostart on|off|toggle` - Manage Windows startup
### C# Version (Backup)

**Build & Run**:
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

**Publish as single-file EXE**:
```bash
dotnet publish pc/SongLedPc -c Release -r win-x64 /p:PublishSingleFile=true
```

**Features**:
- Runs in system tray (no console window)
- Auto-reconnect support
- USB device auto-detection
- Autostart management

### Python Version (Legacy)

**Run**:
```bash
pip install -r pc/requirements.txt
python pc/win_audio_bridge.py --port COM6
```

## UI Controls

- **Rotate encoder**: Move selection or change volume
- **Press encoder**: Confirm / enter
- **K0 button**: Back

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

## Handoff & Troubleshooting

- `docs/HANDOFF.md` - Technical notes and key considerations

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

---

## Documentation

- [Full Multilingual README](README_MULTILINGUAL.md) - Complete docs in English, 日本語, and 中文
- [Documentation Guide](DOCUMENTATION_GUIDE.md) - Language selection
- [GitHub Discussions](https://github.com/CoversiteTT/SongLed/discussions) - Questions and help
