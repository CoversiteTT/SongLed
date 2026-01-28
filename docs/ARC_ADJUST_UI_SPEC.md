# 圆弧参数调节界面规范

> AI快速参考：基于Auto Close Msg (cfgMsgAutoClose) 的标准参数调节界面实现

## 核心要点

- **270度圆弧进度条** (225°-495°)
- **中心显示实际单位值**（如 "5000 ms" 而非滑块值"25"）
- **顶部英文标签**，底部左右刻度对标
- **按键加速调节**，长按保存退出
- **特殊值处理**：0ms显示"INSTANT"，65535ms显示"DISABLED"

## 界面布局示意

```
┌──────────────────────────────────────┐
│      AUTO CLOSE MSG                  │  ← 顶部标签（英文）
│                                      │
│         ╭─────────╮                  │
│      ╭─╯         ╰─╮                │
│    ╭─╯  5000 ms   ╰─╮              │  ← 圆弧进度条 + 中心实际值
│   │               │                │
│    ╰─╮           ╭─╯              │
│      ╰─╮       ╭─╯                │
│         ╰─────────╯                │
│                                      │
│  0 ms      (进度条)      DISABLED    │  ← 左下/右下刻度
└──────────────────────────────────────┘

关键参数:
- 圆弧中心 (cx, cy): cx=width/2, cy=height*0.55
- 圆弧半径 (r): min(w,h)*0.35
- 圆弧范围: start_deg=225°, sweep_deg=270°
- 数值范围: 0ms, 200-11800ms, 65535ms(禁用)
- 滑块范围: 0-60 (内部位置，不显示)
- 进度映射: progress = (毫秒值 - minMs) / (maxMs - minMs)
```

## 数据结构

```cpp
// 全局变量（三件套）
uint16_t cfgMsgAutoCloseMs = 5000;      // 当前生效值（毫秒）
uint16_t cfgMsgAutoCloseMsPending = 5000; // 调整中的临时值
bool cfgMsgAutoCloseMsDirty = false;    // 是否有未保存的修改

// UI组件指针
List *cfgMsgAutoCloseItem = nullptr;
Slider *cfgMsgAutoCloseSlider = nullptr;

// 枚举
enum AdjustTarget {
  ADJ_CFG_CLOSE = 8,  // Auto Close Msg
  // ... 其他参数
};
enum AppMode {
  MODE_CFG_CLOSE_ADJUST = 8,  // Auto Close Msg 调整模式
  // ... 其他模式
};
```

## 映射函数

```cpp
// 毫秒值 → 滑块位置 (0-60)
uint8_t mapCfgCloseFromMs(uint16_t ms) {
  if (ms >= 65535) return 60;      // 禁用 → 滑块60
  if (ms == 0) return 0;           // 立即 → 滑块0
  if (ms < 200) return 1;          // 低值边界
  if (ms > 11800) return 59;       // 高值边界
  return static_cast<uint8_t>(1 + ms / 200);  // 线性映射 200-11800ms → 1-59
}

// 滑块位置 (0-60) → 毫秒值
uint16_t mapCfgCloseMs(uint8_t value) {
  if (value == 0) return 0;              // 滑块0 → 0ms(立即)
  if (value >= 60) return 65535;         // 滑块60 → 65535ms(禁用)
  return static_cast<uint16_t>((value - 1) * 200 + 200);  // 1-59 → 200-11800ms
}
```

## 实现流程

### 1. buildMenus() - 创建菜单项

```cpp
// 创建菜单列表项
cfgMsgAutoCloseItem = new List("Auto Close Msg");

// 计算初始滑块值
uint8_t cfgCloseInitVal = mapCfgCloseFromMs(cfgMsgAutoCloseMs);

// 创建滑块（范围0-60，显示滑块位置而非毫秒值）
cfgMsgAutoCloseSlider = new Slider("Close", 0, 60, 1, cfgCloseInitVal);

// 添加到菜单
menuSettings->addItem(cfgMsgAutoCloseItem, cfgMsgAutoCloseSlider);
```

**关键点**：Slider范围是0-60（滑块位置），不是毫秒值。显示实际毫秒值放在圆弧界面中心。

### 2. handleConfirm() - 进入调整模式

```cpp
void handleConfirm(Menu *current) {
  List *selected = current ? current->getSelected() : nullptr;
  
  if (selected == cfgMsgAutoCloseItem) {
    appMode = MODE_CFG_CLOSE_ADJUST;  // 进入调整模式
    cfgMsgAutoCloseMsPending = cfgMsgAutoCloseMs;  // 临时值 = 当前值
    cfgMsgAutoCloseMsDirty = false;
    updateSettingsWidgets();
    adjustActive = true;
    adjustTarget = ADJ_CFG_CLOSE;
    return;
  }
}
```

### 3. handleKeyEvents() - 按键调整逻辑

**关键创新**：cfgClose **直接操作毫秒值**，加速度×step（1/2/5）

```cpp
// 按键加速计算
auto stepForSpeed = [&](uint32_t nowMs) -> int {
  uint32_t dt = nowMs - lastStepMs;
  lastStepMs = nowMs;
  if (dt <= 60) return 5;    // 快速按：5倍加速
  if (dt <= 140) return 2;   // 中速按：2倍加速
  return 1;                  // 慢速按：1倍（基础）
};

// KEY_0 CLICK: 减小毫秒值
if (keys[key::KEY_0] == key::CLICK) {
  if (adjustMode && adjustCfgClose) {
    uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    int step = stepForSpeed(nowMs);
    
    // 每步200ms × step倍数（快扭时变化量更大）
    uint16_t decrement = static_cast<uint16_t>(200 * step);
    if (cfgMsgAutoCloseMsPending >= decrement) {
      cfgMsgAutoCloseMsPending = static_cast<uint16_t>(cfgMsgAutoCloseMsPending - decrement);
    } else {
      cfgMsgAutoCloseMsPending = 0;  // 最小值：立即关闭
    }
    
    cfgMsgAutoCloseMsDirty = true;
    updateSettingsWidgets();
  }
}

// KEY_1 CLICK: 增大毫秒值
if (keys[key::KEY_1] == key::CLICK) {
  if (adjustMode && adjustCfgClose) {
    uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    int step = stepForSpeed(nowMs);
    
    // 每步200ms × step倍数，按阶段递进
    uint16_t increment = static_cast<uint16_t>(200 * step);
    if (cfgMsgAutoCloseMsPending == 0) {
      cfgMsgAutoCloseMsPending = increment;  // 从立即 → 200/400/1000ms
    } else if (cfgMsgAutoCloseMsPending < 11800) {
      cfgMsgAutoCloseMsPending = static_cast<uint16_t>(cfgMsgAutoCloseMsPending + increment);
      if (cfgMsgAutoCloseMsPending > 11800) cfgMsgAutoCloseMsPending = 11800;  // 上限
    } else {
      cfgMsgAutoCloseMsPending = 65535;  // 到达禁用
    }
    
    cfgMsgAutoCloseMsDirty = true;
    updateSettingsWidgets();
  }
}

// KEY_0/1 PRESS: 保存退出
if (keys[key::KEY_0] == key::PRESS || keys[key::KEY_1] == key::PRESS) {
  if (adjustMode && adjustCfgClose && cfgMsgAutoCloseMsDirty) {
    cfgMsgAutoCloseMs = cfgMsgAutoCloseMsPending;  // 保存当前值
    applyCfgMsgAutoClose();  // 应用设置
    saveSettings();  // 持久化
    appMode = MODE_NORMAL;
    adjustActive = false;
  }
}
```

**加速度细节**：
- 慢扭（dt > 140ms）：±200ms（基础步进）
- 中速（60 < dt ≤ 140ms）：±400ms（2倍）
- 快扭（dt ≤ 60ms）：±1000ms（5倍，最大变化量）

### 4. updateSettingsWidgets() - UI同步

```cpp
void updateSettingsWidgets() {
  // ... 其他参数
  
  if (cfgMsgAutoCloseSlider) {
    // 将毫秒值映射回滑块位置（0-60）显示
    cfgMsgAutoCloseSlider->value = mapCfgCloseFromMs(cfgMsgAutoCloseMsPending);
    cfgMsgAutoCloseSlider->init();  // 必须调用！
  }
}
```

### 5. renderAdjustScreen() - 圆弧渲染

```cpp
// 设定目标参数
if (adjustTarget == ADJ_CFG_CLOSE) {
  targetValue = static_cast<float>(cfgMsgAutoCloseMsPending);
  
  // 关键：禁用值(65535)需要限制为maxValue，否则progress会超过1.0导致圆弧超过270°
  if (cfgMsgAutoCloseMsPending >= 65535) {
    targetValue = 11800.0f;  // 限制进度条到100%
  }
  
  minValue = 0.0f;
  maxValue = 11800.0f;  // 最大有意义的毫秒值（不是65535）
  label = "AUTO CLOSE MSG";
  
  // 关键：中心显示实际毫秒值，特殊值单独处理
  if (cfgMsgAutoCloseMsPending == 0) {
    valueText = "INSTANT";
  } else if (cfgMsgAutoCloseMsPending >= 65535) {
    valueText = "DISABLED";
  } else {
    valueText = std::to_string(cfgMsgAutoCloseMsPending) + " ms";
  }
  
  minText = "0 ms";
  maxText = "DISABLED";
}

// 平滑动画
if (adjustTarget != lastTarget) {
  shownValue = targetValue;
  lastTarget = adjustTarget;
  enterProgress = 0.0f;
}

Animation::move(&shownValue, targetValue, ui.listAnimationSpeed);
Animation::move(&enterProgress, 1.0f, ui.listAnimationSpeed);

// 计算圆弧进度 [0.0, 1.0]
float progress = 0.0f;
if (maxValue > minValue) {
  progress = (shownValue - minValue) / (maxValue - minValue);
}

// 绘制圆弧进度条
HAL::setDrawType(1);  // 描边模式
HALAstraESP32::ArcOverlay overlay {};
overlay.enabled = true;
overlay.cx = cx;
overlay.cy = cy;
overlay.r = r;
overlay.start_deg = 225.0f;   // 左下起始
overlay.sweep_deg = 270.0f;   // 顺时针270度
overlay.progress = progress;  // 进度比例
overlay.thickness = 2;
overlay.aa_samples = 1;
hal.setArcOverlay(overlay);

// 绘制顶部标签、中心数值、刻度
int labelW = HAL::getFontWidth(label);
HAL::drawEnglish(cx - labelW/2, cy - r - 2, label);

int valW = HAL::getFontWidth(valueText);
HAL::drawEnglish(cx - valW/2, cy, valueText);

drawTickLabel(225.0f, minText);   // 左下刻度
drawTickLabel(495.0f, maxText);   // 右下刻度
```

### 6. saveSettings() / loadSettings() - NVS持久化

```cpp
void saveSettings() {
  nvs_handle_t handle;
  nvs_open("storage", NVS_READWRITE, &handle);
  
  nvs_set_u16(handle, "cfg_close_ms", cfgMsgAutoCloseMs);
  
  nvs_commit(handle);
  nvs_close(handle);
}

void loadSettings() {
  nvs_handle_t handle;
  nvs_open("storage", NVS_READONLY, &handle);
  
  uint16_t val = 5000;
  nvs_get_u16(handle, "cfg_close_ms", &val);
  cfgMsgAutoCloseMs = val;
  
  nvs_close(handle);
}
```

### 7. applyCfgMsgAutoClose() - 应用设置

```cpp
void applyCfgMsgAutoClose() {
  hal.setCfgMsgAutoCloseMs(cfgMsgAutoCloseMs);
  // 或其他系统应用方式
}
```

## 所有参数规范检查清单

### Settings菜单中的7个参数

| 参数 | 数据类型 | 范围 | 按键逻辑 | 显示单位 | 状态 |
|------|---------|------|---------|---------|------|
| UI Speed | uint8_t | 1-50 | ±step | "x"倍速 | ✅ |
| Sel Speed | uint8_t | 1-50 | ±step | "ms"毫秒 | ✅ |
| Wrap Pause | uint8_t | 0-50 | ±step | "ms"毫秒 | ✅ |
| Font Color | uint8_t | 0-50 | ±step | "deg"度数 | ✅ |
| Scroll Time | uint8_t | 1-50 | ±step | "ms"毫秒 | ✅ |
| Lyric Speed | uint8_t | 1-30 | ±step | "cps"字符率 | ✅ |
| **Auto Close Msg** | **uint16_t** | **0/200-11800/65535** | **±(200×step)ms** | **"ms"毫秒或特殊值** | ✅ |

### 规范应用验证

- ✅ **中心显示实际单位值**：所有参数都在valueText显示映射后的实际值（非滑块值）
- ✅ **Slider范围合理**：1-50或0-50或0-60的滑块位置范围，不是毫秒值
- ✅ **按键加速**：所有参数都使用stepForSpeed，快速按时变化量×5倍
- ✅ **映射函数分离**：renderAdjustScreen中都使用mapXxx()将滑块值映射为实际值
- ✅ **圆弧进度限制**：所有参数都保证progress ∈ [0.0, 1.0]（270°范围）
- ✅ **updateSettingsWidgets同步**：所有参数都正确更新Slider->value
- ✅ **特殊值处理**：cfgClose的INSTANT/DISABLED已独立处理

## 添加新参数清单

当向settings菜单中添加新参数时，务必遵循此规范流程：

| 特性 | scrollTime | selSpeed | cfgClose |
|------|-----------|----------|---------|
| **数据类型** | uint8_t | uint8_t | **uint16_t** |
| **范围** | 1-50 | 1-50 | 0/200-11800/65535 |
| **Slider范围** | 1-50 | 1-50 | **0-60** |
| **按键逻辑** | ±step (1-5) | ±step (1-5) | **±(200×step) ms** |
| **加速倍数** | 1/2/5 | 1/2/5 | **1/2/5** |
| **映射函数** | 指数递减 | 指数递减 | **线性(ms)** |
| **中心显示** | 映射后毫秒 | 映射后毫秒 | **毫秒值** |
| **特殊值** | 无 | 无 | **INSTANT/DISABLED** |
| **关键创新** | 映射分离 | 映射分离 | **直接毫秒操作×加速** |

## 所有参数规范检查清单

### Settings菜单中的7个参数

| 参数 | 数据类型 | 范围 | 按键逻辑 | 显示单位 | 状态 |
|------|---------|------|---------|---------|------|
| UI Speed | uint8_t | 1-50 | ±step | "x"倍速 | ✅ |
| Sel Speed | uint8_t | 1-50 | ±step | "ms"毫秒 | ✅ |
| Wrap Pause | uint8_t | 0-50 | ±step | "ms"毫秒 | ✅ |
| Font Color | uint8_t | 0-50 | ±step | "deg"度数 | ✅ |
| Scroll Time | uint8_t | 1-50 | ±step | "ms"毫秒 | ✅ |
| Lyric Speed | uint8_t | 1-30 | ±step | "cps"字符率 | ✅ |
| **Auto Close Msg** | **uint16_t** | **0/200-11800/65535** | **±(200×step)ms** | **"ms"毫秒或特殊值** | ✅ |

### 规范应用验证

- ✅ **中心显示实际单位值**：所有参数都在valueText显示映射后的实际值（非滑块值）
- ✅ **Slider范围合理**：1-50或0-50或0-60的滑块位置范围，不是毫秒值
- ✅ **按键加速**：所有参数都使用stepForSpeed，快速按时变化量×5倍
- ✅ **映射函数分离**：renderAdjustScreen中都使用mapXxx()将滑块值映射为实际值
- ✅ **圆弧进度限制**：所有参数都保证progress ∈ [0.0, 1.0]（270°范围）
- ✅ **updateSettingsWidgets同步**：所有参数都正确更新Slider->value
- ✅ **特殊值处理**：cfgClose的INSTANT/DISABLED已独立处理

1. **声明全局变量**：xxxValue, xxxPending, xxxDirty, xxxItem, xxxSlider
2. **枚举定义**：MODE_XXX_ADJUST, ADJ_XXX
3. **映射函数**（如需）：mapXxxFromYyy(), mapXxxToYyy()
4. **buildMenus()**：创建List和Slider
5. **handleConfirm()**：进入调整模式，初始化临时值
6. **handleKeyEvents()**：KEY_0/1 CLICK减增，PRESS保存退出
   - **注意**：如果数据单位是毫秒/百分比等，应直接操作单位值，不通过滑块值中转
7. **updateSettingsWidgets()**：同步slider->value（映射回滑块位置）
8. **renderAdjustScreen()**：设置label/valueText/minText/maxText，映射值显示
9. **saveSettings/loadSettings()**：NVS存取
10. **applyXxx()**：应用到系统

## 关键注意事项

| 项目 | 要求 | 示例 |
|------|------|------|
| **中心数值** | 显示映射后的**实际单位**，不是滑块值 | "5000 ms" 而非 "25" |
| **Slider范围** | 应为合理的滑块位置范围（1-50或0-60） | 0-60（对应0/200-11800/65535ms） |
| **init()调用** | 修改slider->value后**必须调用init()** | `slider->init();` |
| **按键加速** | 快速连按：5步，中速：2步，慢速：1步 | dt≤60→5, dt≤140→2, else→1 |
| **直接操作** | 对于非uint8_t类型，应直接操作实际值 | cfgClose直接-/+200ms，避免映射损失 |
| **边界保护** | 减时检查≥minStep，增时检查≤maxStep | `if (val > step) val -= step; else val = min;` |
| **特殊值** | 端点值（如INSTANT/DISABLED）需单独处理 | valueText = (ms==0) ? "INSTANT" : "..." |

## 代码位置参考

main.cpp:
- 变量声明：第102行 (cfgMsgAutoCloseMs/Pending/Dirty)
- 菜单创建：第1171行 (buildMenus)
- UI同步：第389行 (updateSettingsWidgets)
- 按键逻辑：第903-906行 (KEY_0), 第959-971行 (KEY_1)
- 圆弧渲染：第1768-1783行 (renderAdjustScreen)
- 映射函数：第1285-1298行 (mapCfgCloseFromMs/mapCfgCloseMs)

## 常见问题

**Q: 为什么cfgClose现在用了stepForSpeed?**
A: 为了提高快速旋钮时的反应速度。快扭时可以一次性增减1000ms，避免调整过程繁琐。步进=200×step（慢1×，中2×，快5×）。

**Q: Slider->value应该显示什么？**
A: 显示滑块位置(0-60)，这是内部表示。实际毫秒值显示在圆弧中心(valueText)。

**Q: 为什么maxValue设为11800而不是65535？**
A: 65535是禁用特殊值，不应参与进度条计算。有意义的最大值是11800ms，且进度条必须限制在[0.0, 1.0]范围内（对应270°圆弧）。若targetValue超过maxValue会导致圆弧超度。

**Q: 快扭时变化量是多少？**
A: 慢扭：±200ms，中速：±400ms，快扭：±1000ms（5倍基础步进）。

**Q: 如何在其他参数中复用这个模式？**
A: 如果参数是毫秒/百分比等非uint8_t单位，参考cfgClose：直接操作实际值×step倍数，在renderAdjustScreen中映射显示。
