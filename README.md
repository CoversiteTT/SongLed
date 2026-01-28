# SongLed - ESP32-S3 Volume Knob for Windows 11

**Language**: [English](#english) | [日本語](#日本語) | [中文](#中文)

---

# English

## Overview

Volume control device based on ESP32-S3, ST7789 2.4" TFT, EC11 rotary encoder. Communicates with Windows 11 via USB serial.

Features: Volume control, audio device switching, lyrics display, album artwork.

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

## Firmware Build

Build:
```bash
pio run --environment esp32s3
```

Upload:
```bash
pio run --environment esp32s3 --target upload
```

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

## UI Controls

Rotate encoder: Move / change volume  
Press encoder: Confirm  
K0 button: Back

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

## Project Structure

- `src/` - Firmware (ESP-IDF)
- `pc/` - Windows companion (C#/C++/Python)
- `third_party/` - UI framework & dependencies
- `docs/` - Documentation

## Third-Party Libraries

| Library | License | Purpose |
|---------|---------|---------|
| oled-ui-astra | GPLv3 | UI Framework |
| U8G2 | BSD 3-Clause | Graphics Library |
| ZPIX Font | OFL 1.1 | Chinese Font |

See [THIRD_PARTY.md](THIRD_PARTY.md) for details.

---

# 日本語

## 概要

ESP32-S3、ST7789 2.4" TFT、EC11ロータリーエンコーダベースのボリュームコントロール。Windows 11とUSBシリアル接続で通信。

機能: ボリューム制御、オーディオデバイス切り替え、リリック表示、アルバムアートワーク。

## ハードウェア配線

**TFT (ST7789)**:
- SCL → GPIO12
- SDA → GPIO11
- RES → GPIO7
- DC → GPIO9
- CS → GPIO10
- BLK → GPIO14

**エンコーダ & ボタン**:
- A → GPIO15
- B → GPIO16
- PUSH → GPIO17
- K0 (戻る) → GPIO18

ピンを変更するには `src/main.cpp` を編集してください。

## ファームウェアビルド

ビルド:
```bash
pio run --environment esp32s3
```

アップロード:
```bash
pio run --environment esp32s3 --target upload
```

## Windows ヘルパー

### C++ バージョン

ビルド:
```bash
cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
cmake --build pc/SongLedPcCpp/build --config Release
```

実行:
```bash
pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --port COM6
```

### C# バージョン

実行:
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

公開:
```bash
dotnet publish pc/SongLedPc -c Release -r win-x64 /p:PublishSingleFile=true
```

### Python バージョン

```bash
pip install -r pc/requirements.txt
python pc/win_audio_bridge.py --port COM6
```

## UI 制御

エンコーダ回転: 移動 / 音量変更  
エンコーダ押下: 確認  
K0 ボタン: 戻る

## シリアルプロトコル

ESP32 から:
```
VOL GET
VOL SET <0-100>
MUTE
SPK LIST
SPK SET <index>
HELLO
```

PC から:
```
VOL <0-100>
MUTE <0/1>
SPK BEGIN
SPK ITEM <index> <name>
SPK END
SPK CUR <index>
HELLO OK
```

## プロジェクト構成

- `src/` - ファームウェア (ESP-IDF)
- `pc/` - Windows コンパニオン (C#/C++/Python)
- `third_party/` - UI フレームワーク & 依存関係
- `docs/` - ドキュメント

## サードパーティライブラリ

| ライブラリ | ライセンス | 用途 |
|-----------|-----------|------|
| oled-ui-astra | GPLv3 | UI フレームワーク |
| U8G2 | BSD 3-Clause | グラフィックスライブラリ |
| ZPIX Font | OFL 1.1 | 中国語フォント |

詳細は [THIRD_PARTY.md](THIRD_PARTY.md) を参照してください。

---

# 中文

## 概览

基于 ESP32-S3、ST7789 2.4" TFT、EC11 旋钮的音量控制设备。通过 USB 串口与 Windows 11 通信。

功能: 音量控制、音频设备切换、歌词显示、专辑封面。

## 硬件接线

**TFT (ST7789)**:
- SCL → GPIO12
- SDA → GPIO11
- RES → GPIO7
- DC → GPIO9
- CS → GPIO10
- BLK → GPIO14

**旋钮 & 按钮**:
- A → GPIO15
- B → GPIO16
- PUSH → GPIO17
- K0 (返回) → GPIO18

编辑 `src/main.cpp` 修改引脚。

## 固件编译

编译:
```bash
pio run --environment esp32s3
```

烧录:
```bash
pio run --environment esp32s3 --target upload
```

## Windows 助手

### C++ 版本

编译:
```bash
cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
cmake --build pc/SongLedPcCpp/build --config Release
```

运行:
```bash
pc/SongLedPcCpp/build/Release/SongLedPcCpp.exe --port COM6
```

### C# 版本

运行:
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

发布:
```bash
dotnet publish pc/SongLedPc -c Release -r win-x64 /p:PublishSingleFile=true
```

### Python 版本

```bash
pip install -r pc/requirements.txt
python pc/win_audio_bridge.py --port COM6
```

## UI 操作

旋转旋钮: 移动 / 改变音量  
按下旋钮: 确认  
K0 按钮: 返回

## 串口协议

ESP32 发送:
```
VOL GET
VOL SET <0-100>
MUTE
SPK LIST
SPK SET <index>
HELLO
```

PC 发送:
```
VOL <0-100>
MUTE <0/1>
SPK BEGIN
SPK ITEM <index> <name>
SPK END
SPK CUR <index>
HELLO OK
```

## 项目结构

- `src/` - 固件 (ESP-IDF)
- `pc/` - Windows 伴侣应用 (C#/C++/Python)
- `third_party/` - UI 框架 & 依赖
- `docs/` - 文档

## 第三方库

| 库 | 许可证 | 用途 |
|---|--------|------|
| oled-ui-astra | GPLv3 | UI 框架 |
| U8G2 | BSD 3-Clause | 图形库 |
| ZPIX Font | OFL 1.1 | 中文字体 |

详见 [THIRD_PARTY.md](THIRD_PARTY.md)。

---

## License

GNU General Public License v3.0 - [LICENSE](LICENSE)

[THIRD_PARTY.md](THIRD_PARTY.md) | [Technical Details](docs/HANDOFF.md) | [GitHub](https://github.com/CoversiteTT/SongLed)
