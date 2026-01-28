# SongLed 交接稿（2026-01-28）

## 项目概况
- 硬件: ESP32-S3 N16R8 + 2.4" ST7789 320x240 SPI TFT + EC11 旋钮 + 返回键
- 目标: 通过 USB 串口控制 Win11 音量/输出设备，同时提供菜单+歌词+封面浮窗
- 固件: PlatformIO + ESP-IDF (board: `esp32-s3-devkitc-1`, framework: `espidf`)
- UI: 基于 `third_party/oled-ui-astra` 做适配
- PC 端: C# WinForms/Tray (`pc/SongLedPc`)，另有 C++ 版本草案 (`pc/SongLedPcCpp`)

## 关键路径
- 固件入口: `src/main.cpp`
- HAL: `src/hal_astra_esp32.cpp/.h`
- UI 引擎: `third_party/oled-ui-astra/...`
- 分区: `partitions/`
- PC 端: `pc/SongLedPc`（主用）

## 串口协议要点
- 握手: `HELLO` / `HELLO OK`
- 音量: `VOL <0-100>`, `MUTE <0|1>`
- 输出设备:
  - `SPK BEGIN` / `SPK ITEM <id> <name>` / `SPK END`
  - `SPK CUR <id>`
- 歌词:
  - `LRC CUR <text>` / `LRC NXT <text>` / `LRC CLR`
- NowPlaying:
  - `NP META <title>\t<artist>`
  - `NP PROG <posMs> <durMs>`
  - `NP CLR`
  - 封面:
    - `NP COV BEGIN 40 40`
    - `NP COV DATA <hex565...>` (每像素 4 hex)
    - `NP COV END`

## 目前实现/修复
- nowPlaying 超时清理: 8s 内无 NP 消息则自动隐藏浮窗
- 封面进度条: 根据 `coverIndex / coverTotal` 实时增长
- 封面/进度条背景: 低亮度纯色（已取消渐变）
- 浮窗消失时清理进度条覆盖层
- 设置详情中文说明: 使用 `\u` 方式避免乱码

## 常用命令
- 编译固件:
  - `platformio run --environment esp32s3`
- 烧录固件:
  - `platformio run --environment esp32s3 --target upload`
- PC 端构建:
  - `dotnet build pc\SongLedPc -c Release`

## 可能的坑
- `pc\SongLedPc\bin` 若 exe 正在运行，清理会 “Access is denied”
- 封面显示异常时通常是: 字节序 / coverIndex 未对齐 / coverTotal 未同步
- 进度条残留: 需在 `nowPlaying.active` 变 false 时清 `clearBarOverlay()`

## 设置项（固件）
- UI 动画速度、选择框速度、循环跳转停顿
- 全局字体颜色（Hue）
- 菜单长文本滚动周期
- 歌词滚动速度（字符/秒）

## 目录清理建议（提交前）
- 删除: `.pio/`, `build/`, `pc/SongLedPc/bin`, `pc/SongLedPc/obj`, `pc/SongLedPcCpp/bin`, `pc/SongLedPcCpp/obj`
- 保留: 源码、配置、第三方源码、文档
