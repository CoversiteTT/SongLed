# SongLed - ESP32-S3 Volume Knob for Windows 11

**Languages**: [English](#english-documentation) | [日本語](#日本語ドキュメント) | [中文](#中文文档)

---

# English Documentation

## Overview

Volume control device based on ESP32-S3, ST7789 2.4" TFT, EC11 rotary encoder. Communicates with Windows 11 via USB serial.

Features: Volume control, audio device switching, lyrics display, album artwork.

[Hardware](#hardware-pinout) | [Build](#firmware-build) | [Helper](#windows-helper) | [Protocol](#serial-protocol)

---

# 日本語ドキュメント

## 概要

ESP32-S3、ST7789 2.4" TFT、EC11ロータリーエンコーダベースのボリュームコントロール。Windows 11とUSBシリアル接続で通信。

機能: ボリューム制御、オーディオデバイス切り替え、リリック表示、アルバムアートワーク。

[ハードウェア](#hardware-pinout) | [ビルド](#firmware-build) | [ヘルパー](#windows-helper) | [プロトコル](#serial-protocol)

---

# 中文文档

## 概览

基于 ESP32-S3、ST7789 2.4" TFT、EC11 旋钮的音量控制设备。通过 USB 串口与 Windows 11 通信。

功能: 音量控制、音频设备切换、歌词显示、专辑封面。

[硬件](#hardware-pinout) | [构建](#firmware-build) | [助手](#windows-helper) | [协议](#serial-protocol)

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

Build:
```bash
pio run --environment esp32s3
```

Upload:
```bash
pio run --environment esp32s3 --target upload
```

---

## Windows Helper

### C++ Version

Build:
```bash
cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
cmake --build pc/SongLedPcCpp/build --config Release
```

Run:
```bash
pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --port COM6
```

Options: `--port`, `--vid`, `--pid`, `--autostart on|off|toggle`

### C# Version

Run:
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

Publish:
```bash
dotnet publish pc/SongLedPc -c Release -r win-x64 /p:PublishSingleFile=true
```

### Python Version

```bash
pip install -r pc/requirements.txt
python pc/win_audio_bridge.py --port COM6
```

---

## UI Controls

Rotate encoder: Move / change volume  
Press encoder: Confirm  
K0 button: Back

---

## Serial Protocol

From ESP32:
```
VOL GET
VOL SET <0-100>
MUTE
SPK LIST
SPK SET <index>
HELLO
```

From PC:
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

GNU General Public License v3.0 - See [LICENSE](LICENSE)

### Third-Party Libraries

| Library | License | Purpose |
|---------|---------|---------|
| oled-ui-astra | GPLv3 | UI Framework |
| U8G2 | BSD 3-Clause | Graphics Library |
| ZPIX Font | OFL 1.1 | Chinese Font |

See [THIRD_PARTY.md](THIRD_PARTY.md) for details.

---

More: [Technical Details](docs/HANDOFF.md) | [GitHub](https://github.com/CoversiteTT/SongLed) | [Discussions](https://github.com/CoversiteTT/SongLed/discussions)
