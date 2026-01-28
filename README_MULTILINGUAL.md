# SongLed - ESP32-S3 Volume Knob for Windows 11

**Languages**: [English](#english-documentation) | [日本語](#日本語ドキュメント) | [中文](#中文文档)

---

# English Documentation

## Overview

A feature-rich desktop volume control peripheral based on **ESP32-S3 + ST7789 2.4" TFT + EC11 rotary encoder**. Communicates with Windows 11 via USB serial to control system volume, switch audio output devices, and display lyrics with album artwork.

**Key Features**:
- Rotary encoder volume control
- Audio output device switching
- Real-time lyrics display
- Album artwork visualization
- Persistent settings storage
- OLED UI framework with responsive menus

**Quick Links**: [Hardware Setup](#hardware-pinout) | [Build](#firmware-build) | [Helper](#windows-helper) | [Protocol](#serial-protocol)

---

# 日本語ドキュメント

## 概要

**ESP32-S3 + ST7789 2.4" TFT + EC11ロータリーエンコーダ**ベースの機能豊富なデスクトップボリュームコントロール。Windows 11とUSBシリアル接続でシステムボリュームを制御し、オーディオ出力デバイスを切り替えて、リリックとアルバムアートワークを表示します。

**主な機能**:
- ロータリーエンコーダボリュームコントロール
- オーディオ出力デバイス切り替え
- リアルタイムリリック表示
- アルバムアートワーク表示
- 永続設定保存
- レスポンシブメニュー付きOLED UIフレームワーク

**クイックリンク**: [ハードウェアセットアップ](#hardware-pinout) | [ビルド](#firmware-build) | [ヘルパー](#windows-helper) | [プロトコル](#serial-protocol)

---

# 中文文档

## 概览

基于 **ESP32-S3 + ST7789 2.4" TFT + EC11 旋钮** 的功能丰富的桌面音量控制器。通过 USB 串口与 Windows 11 通信，实现系统音量控制、音频输出设备切换，以及显示歌词和专辑封面。

**主要特性**:
- 旋钮音量控制
- 音频输出设备切换
- 实时歌词显示
- 专辑封面可视化
- 设置持久化存储
- 响应式菜单的 OLED UI 框架

**快速链接**: [硬件设置](#hardware-pinout) | [构建](#firmware-build) | [助手](#windows-helper) | [协议](#serial-protocol)

---

## Hardware Pinout

**TFT (ST7789)**:
- SCL → GPIO12
- SDA → GPIO11
- RES → GPIO7
- DC → GPIO9
- CS → GPIO10
- BLK → GPIO14

**Encoder & Buttons**:
- A → GPIO15
- B → GPIO16
- PUSH → GPIO17
- K0 (back) → GPIO18

Edit `src/main.cpp` to change pins.

---

## Firmware Build

**Build**:
```bash
pio run --environment esp32s3
```

**Upload**:
```bash
pio run --environment esp32s3 --target upload
```

---

## Windows Helper

### C++ Version (Recommended)

```bash
# Build
cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
cmake --build pc/SongLedPcCpp/build --config Release

# Run
pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --port COM6
```

**Options**: `--port`, `--vid`, `--pid`, `--autostart on|off|toggle`

### C# Version (Backup)

```bash
# Run
dotnet run --project pc/SongLedPc -- --port COM6

# Publish
dotnet publish pc/SongLedPc -c Release -r win-x64 /p:PublishSingleFile=true
```

### Python Version (Legacy)

```bash
pip install -r pc/requirements.txt
python pc/win_audio_bridge.py --port COM6
```

---

## UI Controls

- **Rotate encoder**: Move selection or change volume
- **Press encoder**: Confirm / enter
- **K0 button**: Back

---

## Serial Protocol

**From ESP32**:
```
VOL GET
VOL SET <0-100>
MUTE
SPK LIST
SPK SET <index>
HELLO
```

**From PC**:
```
VOL <0-100>
MUTE <0/1>
SPK BEGIN
SPK ITEM <index> <name>
SPK END
SPK CUR <index>
HELLO OK
```

---

## Project Structure

- `src/` - Firmware (ESP-IDF)
- `pc/` - Windows companion (C#/C++/Python)
- `third_party/` - UI framework & dependencies
- `docs/` - Documentation

---

## License

**GNU General Public License v3.0** - See [LICENSE](LICENSE)

### Third-Party Libraries

| Library | License | Purpose |
|---------|---------|---------|
| **oled-ui-astra** | GPLv3 | UI Framework |
| **U8G2** | BSD 3-Clause | Graphics Library |
| **ZPIX Font** | OFL 1.1 | Chinese Font |

See [THIRD_PARTY.md](THIRD_PARTY.md) for details.

---

## More Information

- [Technical Details](docs/HANDOFF.md)
- [GitHub Repository](https://github.com/CoversiteTT/SongLed)
- [GitHub Discussions](https://github.com/CoversiteTT/SongLed/discussions)
