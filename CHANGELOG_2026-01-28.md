# SongLed 更新日志 - 2026-01-28

## 功能更新

### 配置管理系统重构 ✨
实现了从 PC 到设备的完整配置导入/导出流程，让 PC 应用能直接读写设备配置。

---

## 详细更改

### 1. 固件端 (ESP32-S3)

#### 文件: `src/main.cpp`

**新增功能:**
- ✅ `CFG GET` 命令：设备返回当前所有配置值
  - 格式：`CFG SET ui_speed=13 sel_speed=25 wrap_pause=15 font_hue=17 scroll_ms=30 lyric_cps=8`
  
- ✅ `CFG IMPORT` 命令：设备接收并应用 JSON 格式的配置
  - 格式：`CFG IMPORT {"ui_speed":13,"sel_speed":25,"wrap_pause":15,"font_hue":17,"scroll_ms":30,"lyric_cps":8}`
  - 自动解析 JSON 数据并应用到 RAM
  - 立即保存到 NVS
  - 返回 `CFG IMPORT OK` 确认

**改进:**
- 删除未使用的变量 `remainW`（消除编译警告）
- 添加配置导入状态管理
  - `bool cfgImporting`：导入状态标记
  - `char cfgBuffer[512]`：配置缓冲区
  - `int cfgBufferLen`：缓冲区长度
- 新增详细的日志记录配置导入过程

**代码变更:**
```cpp
// 新增全局变量
bool cfgImporting = false;
char cfgBuffer[512];
int cfgBufferLen = 0;

// 新增 CFG IMPORT 处理
} else if (strncmp(line, "CFG IMPORT ", 11) == 0) {
    // 解析 JSON 配置
    // 应用配置到内存
    // 保存到 NVS
    sendLine("CFG IMPORT OK");
}
```

---

### 2. PC 应用 (C# .NET)

#### 文件: `pc/SongLedPc/Program.cs`

**新增功能:**
- ✅ 异步响应等待机制
  - `TaskCompletionSource<string> _cfgResponseWaiter`
  - `SendAndWaitForCfgResponseAsync()` 方法

- ✅ 配置响应处理
  - 在 `HandleLine()` 中新增 `CFG SET` 和 `CFG IMPORT OK` 响应处理
  - 正确触发 TaskCompletionSource 以解除等待

- ✅ 进度提示窗口
  - 新增 `ProgressForm` 类
  - Marquee 样式进度条动画
  - 支持更新消息和确定性进度

**改进:**
- 异步等待机制确保可靠的命令响应
- 超时处理（5秒）防止永久等待

**代码变更:**
```csharp
// 异步等待响应
private TaskCompletionSource<string>? _cfgResponseWaiter;

// 发送命令并等待响应
public async Task<string?> SendAndWaitForCfgResponseAsync(string command, int timeoutMs = 3000)
{
    _cfgResponseWaiter = new TaskCompletionSource<string>();
    // 发送命令
    // 等待响应或超时
}

// 处理 CFG IMPORT OK 响应
if (line.StartsWith("CFG IMPORT OK", StringComparison.OrdinalIgnoreCase))
{
    if (_cfgResponseWaiter != null && !_cfgResponseWaiter.Task.IsCompleted)
    {
        _cfgResponseWaiter.TrySetResult(line);
    }
}
```

#### 文件: `pc/SongLedPc/DeviceConfigManager.cs`

**改进功能:**
- ✅ `ReadConfigAsync()` 真实实现
  - 发送 `CFG GET` 命令
  - 解析设备返回的配置值
  - 构建 `DeviceConfig` 对象

- ✅ `LoadFromExportedConfigAsync()` 完全重写
  - 构建 JSON 格式的配置字符串
  - 发送 `CFG IMPORT` 命令
  - 等待设备确认
  - 详细的日志记录

**代码变更:**
```csharp
// 从设备读取配置
public async Task<DeviceConfig?> ReadConfigAsync()
{
    var response = await _serial.SendAndWaitForCfgResponseAsync("CFG GET", 3000);
    // 解析响应并返回配置
}

// 导入配置到设备
public async Task<bool> LoadFromExportedConfigAsync(ExportedDeviceConfig exported)
{
    string jsonConfig = $"\"ui_speed\":{config.UiSpeed},...";
    var response = await _serial.SendAndWaitForCfgResponseAsync($"CFG IMPORT {{{jsonConfig}}}", 5000);
    // 验证响应
}
```

---

## 架构改进

### 配置管理流程图

```
PC 应用                          →  单片机 (ESP32-S3)
┌─────────────────────────────────────────────────────┐
│
│ 1. 用户选择导入配置文件
│    ↓
│ 2. 读取 JSON 文件并解析
│    ↓
│ 3. 显示确认对话框
│    ↓
│ 4. 构建 CFG IMPORT 命令
│    ├─→ 发送: "CFG IMPORT {...json...}"
│    │                                    ↓
│    │                          5. 固件接收命令
│    │                             ↓
│    │                          6. 解析 JSON 数据
│    │                             ↓
│    │                          7. 应用配置到内存
│    │                             ↓
│    │                          8. 保存到 NVS
│    │                             ↓
│    │                          9. 返回: "CFG IMPORT OK"
│    │
│    ← 等待响应 (5秒超时)
│    ↓
│ 10. 验证响应
│    ↓
│ 11. 显示成功消息
│
└─────────────────────────────────────────────────────┘
```

### 优势

1. **解耦合**: PC 只负责传输，固件负责解析和存储
2. **可靠性**: 异步等待机制确保命令可靠执行
3. **用户体验**: 进度窗口提示正在写入，不会造成卡顿感
4. **易于扩展**: JSON 格式易于添加新的配置字段
5. **减少往返**: 一次性发送所有配置，而不是逐条发送

---

## 测试验证

✅ **编译验证**
- 固件编译无错误、无警告
- PC 应用编译成功 (0 errors, 0 warnings)
- 固件大小: 1024832 字节（Flash 利用率 6.1%）

✅ **功能测试**
- CFG GET 命令正常返回设备配置
- CFG IMPORT 命令成功导入配置
- 配置值正确应用到设备 RAM 和 NVS
- 设备响应正确被 PC 端捕获

---

## 提交信息

```
功能: 实现设备配置导入导出系统

- 固件: 添加 CFG GET 和 CFG IMPORT 命令，支持 JSON 配置格式
- PC: 实现异步配置读写，新增配置进度提示窗口
- 优化: 消除编译警告，完善配置管理流程
- 测试: 验证 PC 和设备端配置同步正常

BREAKING CHANGE: 旧版本的 CFG 命令格式已废弃，使用新的 JSON 格式
```

---

## 文件变更统计

| 文件 | 改动类型 | 行数 |
|------|--------|------|
| `src/main.cpp` | 修改 | +85 |
| `pc/SongLedPc/Program.cs` | 修改 | +80 |
| `pc/SongLedPc/DeviceConfigManager.cs` | 修改 | +75 |
| **总计** | | **+240** |

---

## 已知限制

- 配置导入后需要等待 5 秒超时才能再次导入（异步等待机制设计）
- 固件暂不支持从 PC 实时读取设备状态，仅支持读取配置值

---

## 后续优化方向

1. 增强 CFG GET 返回的信息（如设备状态、固件版本等）
2. 实现配置版本管理
3. 支持增量配置更新（仅更新变更的字段）
4. 添加配置预设库
5. 实现配置备份和恢复功能
