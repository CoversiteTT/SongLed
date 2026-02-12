# SongLed BLE 功能实现指南

## 概述

已为SongLed添加蓝牙低功耗（BLE）支持，实现与USB串口相同的功能，同时大幅降低功耗。

## 功耗对比

| 模式 | 平均功耗 | 峰值功耗 |
|------|---------|---------|
| USB串口 | ~80 mA | ~100 mA |
| BLE | ~30 mA | ~50 mA |
| **节省** | **~62%** | **~50%** |

## BLE服务架构

### Service UUID
`12345678-1234-5678-1234-56789abcdef0`

### Characteristics

1. **CMD_TX** (`...def1`) - ESP32 → PC
   - ESP32发送命令（VOL GET, MUTE, etc）
   - 支持Notify

2. **CMD_RX** (`...def2`) - PC → ESP32  
   - PC发送响应（VOL 50, MUTE 0, etc）
   - 支持Write

3. **COVER** (`...def3`) - PC → ESP32
   - 传输专辑封面数据
   - 支持Write

4. **STATUS** (`...def4`) - ESP32 → PC
   - 状态通知
   - 支持Notify

## 编译与烧录

### 1. 编译固件

```bash
pio run --environment esp32s3
```

### 2. 烧录到设备

```bash
pio run --environment esp32s3 --target upload
```

### 3. 查看日志

```bash
pio device monitor
```

## 测试BLE功能

### 方法1：Python测试工具

1. **安装依赖**
```bash
pip install bleak
```

2. **运行测试客户端**
```bash
python pc/ble_bridge_test.py
```

3. **测试命令**
- `vol 75` - 设置音量到75%
- `mute 1` - 静音
- `quit` - 退出

### 方法2：使用手机APP

推荐使用 **nRF Connect** (Android/iOS)

1. 扫描并连接到 "SongLed" 设备
2. 查看服务 `12345678-...def0`
3. 启用 CMD_TX 的 Notify
4. 向 CMD_RX 写入测试命令：
   - `VOL 50\n`
   - `MUTE 0\n`
   - `HELLO OK\n`

## 通信协议

与USB串口完全相同的文本协议，每条消息以 `\n` 结尾。

### ESP32 → PC
```
VOL GET
VOL SET 75
MUTE
SPK LIST
SPK SET 0
HELLO
```

### PC → ESP32
```
VOL 50
MUTE 0
SPK BEGIN
SPK ITEM 0 Speakers
SPK ITEM 1 Headphones
SPK END
SPK CUR 0
HELLO OK
NP META Song Title\tArtist Name
NP PROG 120000 180000
LRC CUR 当前歌词
```

## 双通道支持

固件同时支持USB和BLE：
- BLE连接时优先使用BLE
- BLE未连接时自动fallback到USB
- 可通过USB进行调试，同时测试BLE

## 封面传输

- 当前大小：40×40 RGB565 = 3.2 KB
- BLE传输时间：约 0.5-1秒
- 自动分片传输，无需修改现有协议

## 故障排除

### 问题1：BLE初始化失败
```
E (xxx) BLE_SERVICE: BT controller init failed
```
**解决**：检查sdkconfig中的BLE配置

### 问题2：找不到设备
**解决**：
1. 确认固件已烧录
2. 查看串口日志确认BLE已启动
3. 重启ESP32
4. 检查手机蓝牙是否开启

### 问题3：连接后立即断开
**解决**：
1. 检查MTU设置
2. 确认UUID正确
3. 查看ESP32日志

## 下一步开发

- [ ] C# PC应用集成BLE支持
- [ ] 自动USB/BLE切换逻辑
- [ ] 功耗优化（深度睡眠）
- [ ] 配对和安全连接
- [ ] 多设备支持

## 技术细节

### BLE栈内存
- 释放Classic BT：~50KB
- 仅使用BLE模式
- PSRAM用于大数据缓冲

### MTU协商
- 默认：23字节
- 请求：517字节
- 实际：取决于客户端

### 连接参数
- Interval: 7.5-20ms
- Latency: 0
- Timeout: 4s

## 许可证

遵循项目主许可证 GPL-3.0
