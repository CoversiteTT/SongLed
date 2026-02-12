# SongLed BLE 连接稳定性问题 - 工作交接文档

**更新时间**: 2026年2月12日  
**交接来源**: GitHub Copilot (Claude Haiku 4.5)  
**问题状态**: 🔴 连接在30秒后断开 (原因: 0x13 Device not found)

---

## 📊 当前状态总结

### 已解决问题
- ✅ BLE设备可被发现（设备名: "SongLed"）
- ✅ 客户端可成功连接（日志: "Client connected, conn_id: 0"）
- ✅ MTU协商成功（517字节）
- ✅ GATT特性正确注册，包含CCCD描述符
- ✅ Bluetooth菜单已添加到固件UI（Settings → Bluetooth）
- ✅ PC端多设备选择UI已实现（ble_bridge_test.py）
- ✅ 编码器任务WDT修复（延迟从2ms改为10ms）

### 未解决的关键问题
- ❌ **连接在~30秒后以错误代码0x13断开**
  - 日志模式:
    ```
    I (67938) BLE_SERVICE: Conn params: status=0, min=24 max=48, latency=0 timeout=400
    I (68778) BLE_SERVICE: Conn params: status=0, min=0 max=0, latency=0 timeout=400
    [... 30秒成功的APP LIVE/HELLO交换 ...]
    E (97098) BT_BTM: Device not found
    I (97098) BLE_SERVICE: Client disconnected, reason=0x13
    ```
  - **根本原因**: 客户端第二次协商参数时使用min=0/max=0，可能导致连接超时或协议错误

---

## 🔧 重要代码位置

### 1. BLE服务实现 (`src/ble_service.cpp`)
**关键修改**:
- 添加了CCCD描述符（行~450）
- 添加了`cmd_tx_notify_enabled`和`cmd_rx_seen`标志（行~50）
- 通知双重门控逻辑（行~600）:
  ```cpp
  if (!ble_is_connected || !cmd_tx_notify_enabled || !cmd_rx_seen) {
    return false;
  }
  ```
- 连接参数更新日志（`gap_event_handler`中）
- 断开连接原因日志（行~550）

**关键函数**:
- `ble_send_line()`: 发送BLE通知
- `gatts_event_handler()`: 路由WRITE/READ事件
- `gap_event_handler()`: 记录连接参数和断开原因

**需要关注的部分**:
- 是否需要禁用客户端端参数协商？
- 是否应该在收到min=0/max=0时强制更新参数？

### 2. 主应用 (`src/main.cpp`)
**关键修改**:
- 添加了menuBluetooth菜单（Settings → Bluetooth）
- 添加了Enable/Restart/Disconnect选项
- `sendLine()`函数路由到BLE或USB

**调用关系**:
```
Main Loop → handleConfirm(menuBluetooth)
         → ble_service_init() / ble_restart_advertising()
         → sendLine() → ble_send_line()
```

### 3. PC端测试客户端 (`pc/ble_bridge_test.py`)
**最新增强**:
- 多设备选择UI（如果扫描到多个SongLed）
- 连接后立即发送"HELLO OK"解锁ESP32
- 5秒间隔的心跳保活任务（`_keepalive_task()`）
- 异步消息接收循环

**使用方法**:
```bash
pip install bleak
python pc/ble_bridge_test.py
```

**预期行为**:
1. 扫描SongLed设备
2. 提示用户选择设备
3. 连接并启用通知
4. 每5秒发送"HELLO OK"保活
5. 接收并打印来自ESP32的消息

### 4. 硬件配置 (`sdkconfig.esp32s3`)
**关键配置**:
- 第781行: `CONFIG_BT_BLE_50_FEATURES_SUPPORTED=n` ✅
- 第782行: `CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y` ✅
- **原因**: BLE 5.0禁用了`esp_ble_gap_start_advertising()`和其他GAP API

---

## 🎯 调试思路和假设

### 假设1: Windows BLE栈参数协商bug
- 观察到: 客户端先协商min=24/max=48，然后协商min=0/max=0
- 可能原因: Windows GATT堆栈bug或意外的断线触发
- **建议修复**: 在`gap_event_handler()`中检测min=0/max=0，主动发起disconnect或logging

### 假设2: 连接超时设置不匹配
- 观察到: 两次参数协商之间有~840ms（68778-67938）
- 可能原因: 超时设置(400*10ms=4s)与某个间隔不对齐
- **建议修复**: 尝试增加timeout值到500-600

### 假设3: 心跳/保活缺失
- 观察到: 第一次成功exchange后30秒才断开
- 已实现修复: `ble_bridge_test.py`中的5秒保活
- **建议测试**: 验证Python测试客户端是否能稳定保持>30秒

### 假设4: 通知队列溢出或资源泄漏
- 可能原因: 大量APP LIVE消息堆积导致BLE堆栈崩溃
- **建议检查**: 监控ESP32堆内存和BLE buffer使用情况

---

## 📋 下一步行动 (优先级排序)

### 🚨 URGENT (阻塞)
1. **运行Python测试客户端进行连接稳定性测试**
   ```bash
   python pc/ble_bridge_test.py
   ```
   - 记录是否仍在~30秒后断开
   - 如能稳定>60秒，说明是Windows/.NET BLE栈问题而非ESP32问题

2. **收集完整的ESP32日志和Windows蓝牙日志**
   - 启用ESP32的`D (TIMESTAMP) BLE_SERVICE: ...`调试日志
   - 在Windows中用`logman`收集蓝牙堆栈跟踪

### 📊 HIGH (关键)
3. **分析客户端参数协商异常**
   - 在`gap_event_handler()`中添加详细日志：
     ```cpp
     if (conn_params->min_int == 0 && conn_params->max_int == 0) {
       ESP_LOGW(TAG, "Client negotiated zero params! Forcing update...");
       // 考虑主动更新或断开
     }
     ```

4. **PC端.NET客户端集成**
   - 将ble_bridge_test.py的心跳逻辑移植到SongLedPc.csproj
   - 使用C#的Task.Delay(5000)实现5秒保活

### 🔧 MEDIUM (可选)
5. **配置ESP32连接参数微调**
   - 在`ble_service_init()`中调整: min_int (0x18) / max_int (0x30) / timeout (400)
   - 尝试增加timeout到500-600

6. **增加蓝牙菜单功能**
   - 显示当前连接状态和参数
   - 添加"参数诊断"菜单项显示min/max/latency/timeout

---

## 🧪 测试步骤 (按顺序执行)

### 测试场景1: Python客户端稳定性 (15分钟)
```
1. 编译并上传最新固件
   platformio.exe run --target upload --environment esp32s3

2. 启动PC Python客户端
   python pc/ble_bridge_test.py

3. 选择SongLed设备并连接

4. 观察并计时
   - 能否维持>30秒连接？
   - 心跳信息是否每5秒一次？
   - 是否有异常日志？

5. 记录
   - 断开时间点
   - ESP32日志中的最后一条消息
   - Windows事件查看器中的蓝牙错误
```

### 测试场景2: 多连接尝试 (10分钟)
```
1. 连接 → 等待30秒 → 记录结果 (3次重复)

2. 变量控制
   - 变量A: 是否发送"HELLO OK"？
   - 变量B: APP LIVE消息频率？
   - 变量C: 保活间隔时间？
```

### 测试场景3: .NET客户端测试 (之后)
```
- 将心跳逻辑集成到SongLedPc后重复测试场景1
```

---

## 📝 技术笔记

### BLE连接参数详解
```
min_int:  最小连接间隔 = value * 1.25ms
          0x18 (24) = 30ms ✅ 合理
max_int:  最大连接间隔 = value * 1.25ms
          0x30 (48) = 60ms ✅ 合理
latency:  从连接事件跳过的数量 = 0 (无跳过) ✅
timeout:  超时值 = value * 10ms
          400 = 4秒 ✅ 合理

警告: min=0, max=0 表示参数协商失败或客户端发送无效值
```

### CCCD (Client Characteristic Configuration Descriptor) 必需性
- BLE规范要求: 任何支持通知/指示的特性必须有CCCD
- 客户端必须先向CCCD写入0x0001 (启用通知) 后，服务器才能发送通知
- 违反此规则会导致**远程立即断开(0x13)**

### 错误代码0x13解释
- `HCI_ERROR_UNK_CONN_ID = 0x02` (连接ID未知)
- `HCI_ERROR_UNSPECIFIED = 0x1f` (未指定)
- `HCI_ERROR_DEVICE_NOT_FOUND = 0x13` **← 当前**
  - 含义: 蓝牙控制器找不到该连接
  - 原因: 通常是协议违反或资源耗尽

---

## 📌 关键文件修改历史

| 文件 | 修改内容 | 状态 |
|-----|--------|------|
| `src/ble_service.cpp` | CCCD描述符、cmd_rx_seen标志、双重门控 | ✅ |
| `src/main.cpp` | Bluetooth菜单、参数传递 | ✅ |
| `src/hal_astra_esp32.cpp` | encoder_task延迟10ms | ✅ |
| `sdkconfig.esp32s3` | BLE 4.2启用、BLE 5.0禁用 | ✅ |
| `pc/ble_bridge_test.py` | 多设备UI、5秒心跳保活 | ✅ |
| `pc/SongLedPc.csproj` | **未集成** (待迁移) | ⏳ |

---

## 🔗 相关文档链接
- [docs/AGENT_HANDOFF.md](AGENT_HANDOFF.md) - 之前的工作交接
- [docs/HANDOFF.md](HANDOFF.md) - 更早的交接记录
- platformio.ini - 编译配置

---

## 📞 快速参考命令

```powershell
# 编译并上传
cd C:\Users\21733\Desktop\songled
platformio.exe run --target upload --environment esp32s3

# 监控串口输出
platformio.exe device monitor --baud 115200

# 运行Python测试
pip install bleak
python pc/ble_bridge_test.py
```

---

## ⚠️ 已知限制和风险

1. **BLE 5.0被禁用**: 使用BLE 4.2可能限制某些新设备的兼容性
2. **Windows BLE堆栈未知行为**: 参数协商min=0/max=0是否与Windows特定版本相关？
3. **未测试多设备场景**: 如果有多个PC连接怎么办？
4. **Python心跳是临时方案**: 最终需要在.NET中重现相同逻辑

---

**交接完成。新助手请按照"下一步行动"部分优先级推进。如有疑问，参考本文档的"技术笔记"和"调试思路"。**
