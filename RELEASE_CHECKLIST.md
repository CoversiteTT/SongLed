# GitHub 发布前检查清单

在将项目上传到 https://github.com/CoversiteTT/SongLed 前，请按照以下步骤操作：

## 📋 必需步骤

### 1. 清理不必要的文件
在项目根目录运行清理脚本：

**Windows:**
```cmd
cleanup.bat
```

**Linux/macOS:**
```bash
bash cleanup.sh
```

这将清除：
- `.pio/` - PlatformIO 构建缓存
- `build/` - CMake 构建目录
- `pc/*/bin/` 和 `obj/` - C# 和 C++ 构建产物
- `.vscode/ipch/` - VS Code 智能感知缓存
- `__pycache__/` - Python 缓存
- 所有 `.log` 文件

### 2. 验证许可证合规性 ✅

已包含文件：
- ✅ `LICENSE` - 项目许可证 (GPLv3)
- ✅ `THIRD_PARTY.md` - 第三方库详细说明
- ✅ `README.md` - 已更新的项目说明，包含许可证和依赖信息

### 3. 第三方库确认 ✅

本项目正确使用并致谢了三个核心库：

| 库 | 源代码 | 许可证 | 位置 |
|----|--------|--------|------|
| **oled-ui-astra** | https://github.com/dcfsswindy/oled-ui-astra | GPLv3 | `third_party/oled-ui-astra/` |
| **U8G2** | https://github.com/olikraus/u8g2 | BSD 3-Clause | `third_party/oled-ui-astra/.../u8g2/` |
| **ZPIX Font** | https://github.com/SolidZORO/zpix-pixel-font | OFL 1.1 | `src/u8g2_font_zpix.c` |

所有第三方库的许可证都与项目的 **GPLv3** 许可证兼容。

### 4. 提交清单

确保以下项都已包含：

- ✅ `src/` - 固件源代码
- ✅ `pc/` - Windows 伴侣应用（C#、C++、Python）
- ✅ `third_party/oled-ui-astra/` - UI 框架及子依赖
- ✅ `partitions/` - ESP32 分区配置
- ✅ `docs/` - 文档和图片
- ✅ `LICENSE` - GPLv3 许可证文本
- ✅ `README.md` - 项目文档
- ✅ `THIRD_PARTY.md` - 第三方库属性说明
- ✅ `platformio.ini` - PlatformIO 配置
- ✅ `CMakeLists.txt` - CMake 构建配置
- ✅ `.gitignore` - Git 忽略规则
- ✅ `.github/workflows/` (可选) - CI/CD 配置

### 5. Git 提交

```bash
# 确保所有已跟踪文件已提交
git status

# 阶段所有更改
git add .

# 提交变更
git commit -m "Initial public release with proper licensing and third-party attribution"

# 推送到 GitHub
git push origin main
```

## ⚖️ 许可证重要说明

**本项目采用 GPLv3 许可证**，这是因为：

1. **oled-ui-astra** 使用 GPLv3
   - 任何使用或修改 GPLv3 代码的项目必须也采用 GPLv3
   - 这是 copyleft 许可证的要求

2. **含义：**
   - ✅ 任何人都可以自由使用、修改、分发此项目
   - ✅ 如果分发修改版本，必须在 GPLv3 下发布
   - ✅ 必须提供源代码
   - ❌ 不能将此代码整合到专有软件中（除非得到例外许可）

3. **兼容性：**
   - ✅ BSD 许可证库（U8G2、PySerial）兼容 GPLv3
   - ✅ MIT 许可证库（System.*）兼容 GPLv3
   - ✅ OFL 字体许可证兼容 GPLv3
   - ✅ Apache 2.0 许可证（NAudio）兼容 GPLv3

## 📝 发布后维护

发布后请：

1. **保持致谢**
   - 在 README.md 中保留第三方库链接和属性说明
   - 定期审查第三方库更新

2. **许可证遵守**
   - 任何新的第三方依赖都必须在 THIRD_PARTY.md 中列出
   - 确保新依赖与 GPLv3 兼容

3. **文档更新**
   - 在提交中说明您对第三方代码所做的修改
   - 保持 THIRD_PARTY.md 为最新状态

## ✨ 建议的 GitHub 描述

用于 GitHub 仓库描述：

```
ESP32-S3 Desktop Volume Knob for Windows 11
A feature-rich desktop volume control and audio device switcher with 
ST7789 2.4" display, rotary encoder, lyrics display, and album art support.
Built on oled-ui-astra UI framework.

Licensed under GPLv3. See LICENSE and THIRD_PARTY.md for details.
```

## 🔗 有用的链接

- GPLv3 许可证：https://www.gnu.org/licenses/gpl-3.0.html
- 开源许可证兼容性：https://www.gnu.org/licenses/license-list.html
- SPDX 许可证标识符：https://spdx.org/licenses/

---

**准备好发布！** 🚀
