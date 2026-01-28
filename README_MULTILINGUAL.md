# SongLed - ESP32-S3 Volume Knob for Windows 11

## 🌍 Language Selection | 言語選択 | 语言选择

**English** | [日本語](#-日本語ドキュメント) | [中文](#-中文文档)

> 👉 Select your language above | 上から言語を選択してください | 请从上方选择语言

---

# 📖 English Documentation

## Overview

A feature-rich desktop volume control peripheral based on **ESP32-S3 + ST7789 2.4" TFT + EC11 rotary encoder**. Communicates with Windows 11 via USB serial to control system volume, switch audio output devices, and display lyrics with album artwork in a floating window.

- **Firmware**: ESP-IDF + PlatformIO
- **PC Companion**: C# (primary), C++ & Python alternatives available
- **License**: GNU General Public License v3.0 (GPLv3)

### ✨ Key Features
- 🎚️ Rotary encoder volume control
- 🔊 Audio output device switching  
- 📱 Real-time lyrics display
- 🎨 Album artwork visualization
- 💾 Persistent settings storage
- ⚡ Low-latency serial protocol
- 🎯 OLED UI framework with responsive menus

### 📋 Quick Start

**1. Hardware Setup**
```
TFT (ST7789):     GPIO12(SCL), GPIO11(SDA), GPIO7(RES), GPIO9(DC), GPIO10(CS), GPIO14(BLK)
Encoder/Buttons:  GPIO15(A), GPIO16(B), GPIO17(PUSH), GPIO18(K0-back)
```

**2. Build Firmware**
```bash
platformio run --environment esp32s3
```

**3. Upload Firmware**
```bash
platformio run --environment esp32s3 --target upload
```

**4. Run PC Companion (C#)**
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

### 🔌 Serial Protocol

| Direction | Command |
|-----------|---------|
| ESP32 → PC | `VOL GET`, `SPK LIST`, `HELLO` |
| PC → ESP32 | `VOL <0-100>`, `SPK ITEM`, `HELLO OK` |

Full protocol: [HANDOFF.md](docs/HANDOFF.md)

### 📁 Project Structure
- `src/` - Firmware (ESP-IDF)
- `pc/` - Windows companion (C#/C++/Python)
- `third_party/` - UI framework & dependencies
- `docs/` - Documentation

### 📚 Documentation
- [HANDOFF.md](docs/HANDOFF.md) - Technical handoff notes
- [THIRD_PARTY.md](THIRD_PARTY.md) - Third-party libraries
- [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) - Release guide

### 📄 License
**GNU General Public License v3.0** - See [LICENSE](LICENSE)

---

# 📖 日本語ドキュメント

## 概要

**ESP32-S3 + ST7789 2.4" TFT + EC11ロータリーエンコーダ**ベースの機能豊富なデスクトップボリュームコントロール。Windows 11とUSBシリアル接続でシステムボリュームを制御し、オーディオ出力デバイスを切り替えて、リリックとアルバムアートワークをフローティングウィンドウに表示します。

- **ファームウェア**: ESP-IDF + PlatformIO
- **Windows用コンパニオン**: C#（主要）、C++ & Pythonの代替あり
- **ライセンス**: GNU General Public License v3.0 (GPLv3)

### ✨ 主な機能
- 🎚️ ロータリーエンコーダボリュームコントロール
- 🔊 オーディオ出力デバイス切り替え
- 📱 リアルタイムリリック表示
- 🎨 アルバムアートワーク表示
- 💾 永続設定保存
- ⚡ 低レイテンシシリアルプロトコル
- 🎯 レスポンシブメニュー付きOLED UIフレームワーク

### 📋 クイックスタート

**1. ハードウェアセットアップ**
```
TFT (ST7789):     GPIO12(SCL), GPIO11(SDA), GPIO7(RES), GPIO9(DC), GPIO10(CS), GPIO14(BLK)
エンコーダ/ボタン: GPIO15(A), GPIO16(B), GPIO17(PUSH), GPIO18(K0-戻る)
```

**2. ファームウェアをビルド**
```bash
platformio run --environment esp32s3
```

**3. ファームウェアをアップロード**
```bash
platformio run --environment esp32s3 --target upload
```

**4. PCコンパニオンを実行（C#）**
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

### 🔌 シリアルプロトコル

| 方向 | コマンド |
|------|---------|
| ESP32 → PC | `VOL GET`, `SPK LIST`, `HELLO` |
| PC → ESP32 | `VOL <0-100>`, `SPK ITEM`, `HELLO OK` |

完全なプロトコル: [HANDOFF.md](docs/HANDOFF.md)

### 📁 プロジェクト構成
- `src/` - ファームウェア (ESP-IDF)
- `pc/` - Windowsコンパニオン (C#/C++/Python)
- `third_party/` - UIフレームワーク & 依存関係
- `docs/` - ドキュメント

### 📚 ドキュメント
- [HANDOFF.md](docs/HANDOFF.md) - 技術的引き継ぎノート
- [THIRD_PARTY.md](THIRD_PARTY.md) - サードパーティライブラリ
- [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) - リリースガイド

### 📄 ライセンス
**GNU General Public License v3.0** - [LICENSE](LICENSE)を参照

---

# 📖 中文文档

## 项目介绍

基于 **ESP32-S3 + ST7789 2.4" TFT + EC11 旋钮** 的功能丰富的桌面音量控制器。通过 USB 串口与 Windows 11 通信，实现系统音量控制、音频输出设备切换，以及在浮窗中显示歌词和专辑封面。

- **固件**: ESP-IDF + PlatformIO
- **Windows 伴侣应用**: C#（主要）、C++ 和 Python 备选
- **许可证**: GNU General Public License v3.0 (GPLv3)

### ✨ 主要特性
- 🎚️ 旋钮音量控制
- 🔊 扬声器输出切换
- 📱 实时歌词显示
- 🎨 专辑封面可视化
- 💾 设置持久化存储
- ⚡ 低延迟串口协议
- 🎯 响应式菜单的 OLED UI 框架

### 📋 快速开始

**1. 硬件连接**
```
TFT (ST7789):      GPIO12(SCL), GPIO11(SDA), GPIO7(RES), GPIO9(DC), GPIO10(CS), GPIO14(BLK)
旋钮/按钮:         GPIO15(A), GPIO16(B), GPIO17(按下), GPIO18(K0-返回)
```

**2. 编译固件**
```bash
platformio run --environment esp32s3
```

**3. 烧录固件**
```bash
platformio run --environment esp32s3 --target upload
```

**4. 运行 Windows 伴侣应用（C#）**
```bash
dotnet run --project pc/SongLedPc -- --port COM6
```

### 🔌 串口协议

| 方向 | 命令 |
|------|------|
| ESP32 → PC | `VOL GET`, `SPK LIST`, `HELLO` |
| PC → ESP32 | `VOL <0-100>`, `SPK ITEM`, `HELLO OK` |

完整协议: [HANDOFF.md](docs/HANDOFF.md)

### 📁 项目结构
- `src/` - 固件 (ESP-IDF)
- `pc/` - Windows 伴侣应用 (C#/C++/Python)
- `third_party/` - UI 框架 & 依赖
- `docs/` - 文档

### 📚 文档
- [HANDOFF.md](docs/HANDOFF.md) - 技术交接文档
- [THIRD_PARTY.md](THIRD_PARTY.md) - 第三方库说明
- [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) - 发布指南

### 📄 许可证
**GNU General Public License v3.0** - 详见 [LICENSE](LICENSE)

---

## 🔧 Hardware Details / ハードウェア詳細 / 硬件详情

### Wiring Diagram / 配線図 / 接线图

**TFT Display (ST7789):**
| Pin | GPIO |
|-----|------|
| SCL | 12 |
| SDA | 11 |
| RES | 7 |
| DC | 9 |
| CS | 10 |
| BLK | 14 |

**Rotary Encoder & Buttons:**
| Component | GPIO |
|-----------|------|
| Encoder A | 15 |
| Encoder B | 16 |
| Encoder Push | 17 |
| Back Button (K0) | 18 |

---

## 🛠️ Development / 開発 / 开发

### Requirements / 要件 / 要求
- ESP32-S3 development board
- Python 3.8+
- PlatformIO CLI or VS Code extension
- .NET 8 SDK (for C# app)
- CMake 3.20+ (for C++ app)

### Build Steps / ビルド手順 / 构建步骤

**Firmware:**
```bash
cd <project-root>
platformio run --environment esp32s3
```

**Windows Companion (C#):**
```bash
dotnet build pc/SongLedPc -c Release
```

**Windows Companion (C++):**
```bash
cmake -S pc/SongLedPcCpp -B pc/SongLedPcCpp/build
cmake --build pc/SongLedPcCpp/build --config Release
```

---

## 📚 Third-Party Libraries / サードパーティライブラリ / 第三方库

| Library | License | Purpose |
|---------|---------|---------|
| **oled-ui-astra** | GPLv3 | UI Framework |
| **U8G2** | BSD 3-Clause | Graphics Library |
| **ZPIX Font** | OFL 1.1 | Chinese Font |

See [THIRD_PARTY.md](THIRD_PARTY.md) for details.

---

## 📞 Support / サポート / 支持

- **Issues**: [GitHub Issues](https://github.com/CoversiteTT/SongLed/issues)
- **Discussions**: [GitHub Discussions](https://github.com/CoversiteTT/SongLed/discussions)
- **Email**: Create an issue for inquiries

---

## 📄 License / ライセンス / 许可证

This project is licensed under the **GNU General Public License v3.0**.

See [LICENSE](LICENSE) for the full text.

---

**Made with ❤️ for audio enthusiasts | オーディオ愛好家のために ❤️ で作成 | 为音乐爱好者精心打造 ❤️**
