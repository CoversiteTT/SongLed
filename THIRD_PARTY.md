# Third-Party Libraries and Attributions

This document details all third-party code incorporated and modified in the SongLed project.

## 1. oled-ui-astra UI Framework

**Source**: https://github.com/dcfsswindy/oled-ui-astra  
**License**: GNU General Public License v3.0 (GPLv3)  
**Location**: `third_party/oled-ui-astra/`  
**Original Author**: dcfsswindy  

### Usage
The oled-ui-astra framework is the core UI engine for the ESP32-S3 display. It provides:
- Menu system
- Widget framework
- Display rendering engine
- Font support

### Modifications
The following modifications have been made to adapt it for this project:

1. **Hardware Abstraction Layer (HAL)**
   - Created `src/hal_astra_esp32.cpp/.h` to adapt HAL interface for ESP32-S3
   - Implemented SPI display driver for ST7789 2.4" TFT
   - Integrated GPIO handling for EC11 encoder and buttons
   - Added display overlay features (arc, bar, image overlays)

2. **Menu Customization**
   - Menu structure customized in `src/main.cpp`
   - Added nowPlaying widget for music information display
   - Integrated serial communication handlers for volume/speaker control

3. **Display Features**
   - Added progress bar overlay for cover image loading
   - Implemented nowPlaying timeout (8s auto-hide)
   - Added lyrics scrolling support

### License Compliance
As this project modifies oled-ui-astra, the entire project must be distributed under **GPLv3** to comply with the framework's license.

---

## 2. U8G2 Graphics Library

**Source**: https://github.com/olikraus/u8g2  
**License**: BSD 3-Clause License  
**Location**: `third_party/oled-ui-astra/Core/Src/hal/hal_dreamCore/components/oled/graph_lib/u8g2/`  
**Original Author**: Oliver Kraus (olikraus)  

### Usage
U8G2 provides:
- Graphics rendering primitives (lines, circles, boxes)
- Font handling and rendering
- Display buffering and DMA support
- Built-in fonts (zpix, default fonts)

### Integration
- Integrated as a component within oled-ui-astra
- Used by the display backend for all graphics operations
- Custom font support added for future lyrics display

### License Compliance
The BSD 3-Clause License is compatible with GPLv3. Full license text is available in the U8G2 repository.

---

## 3. ZPIX Pixel Font

**Source**: https://github.com/SolidZORO/zpix-pixel-font  
**License**: OFL (Open Font License) 1.1  
**Location**: Font data in `src/u8g2_font_zpix.c`  
**Original Author**: SolidZORO  

### Usage
ZPIX is a beautiful pixel font planned for Chinese lyrics display on the 2.4" display.

### Current Status
- Font data already converted to U8G2 format
- Currently **not active** in the build (see `src/CMakeLists.txt`)
- Included for future lyrics feature implementation

### License Compliance
The OFL 1.1 is compatible with GPLv3. Font usage does not impose additional restrictions.

---

## PC Dependencies

### C# Version (SongLedPc)

**Location**: `pc/SongLedPc/`

Dependencies declared in `SongLedPc.csproj`:

1. **NAudio** (2.2.1) - Apache 2.0 License
   - Audio device enumeration
   - SMTC (System Media Transport Controls) bridge
   - Source: https://github.com/naudio/NAudio

2. **Microsoft.Windows.SDK.NET** (10.0.18362.6-preview)
   - Windows API interop
   - Licensed under Microsoft License

3. **System.Management** (8.0.0) - MIT License
   - Device management
   - .NET System library

4. **System.IO.Ports** (8.0.0) - MIT License
   - Serial communication
   - .NET System library

### C++ Version (SongLedPcCpp)

**Location**: `pc/SongLedPcCpp/`

Dependencies:

1. **Win32 API** (System libraries)
   - `ole32` - OLE library
   - `oleaut32` - OLE Automation
   - `uuid` - UUID library
   - `setupapi` - Device installation
   - `shell32` - Windows shell API
   - `shlwapi` - Shell lightweight utility

2. **Standard C++ Library**
   - No external dependencies beyond system libraries

### Python Version (Legacy)

**Location**: `pc/win_audio_bridge.py`

Dependencies in `pc/requirements.txt`:

1. **PySerial** - BSD License
   - Serial port communication
   - Source: https://github.com/pyserial/pyserial

---

## Summary of Licenses

| Component | License | Type | Remarks |
|-----------|---------|------|---------|
| oled-ui-astra | GPLv3 | Copyleft | Primary dependency, mandates GPLv3 for project |
| U8G2 | BSD 3-Clause | Permissive | Compatible with GPLv3 |
| ZPIX Font | OFL 1.1 | Permissive | Font license, no code restrictions |
| NAudio | Apache 2.0 | Permissive | C# dependency only |
| PySerial | BSD | Permissive | Python helper only |

---

## How to Comply with Licenses

When distributing or modifying this project:

1. **Include LICENSE file** - GPLv3 text is included in repository root
2. **Preserve notices** - All third-party license notices are preserved in their original locations
3. **Document modifications** - Changes to third-party code are documented here
4. **Provide source** - When distributing binaries, provide access to source code
5. **Maintain GPLv3** - If you modify and distribute, keep the project under GPLv3

---

## Contributing Guidelines

If you contribute to SongLed:

- Ensure you understand and accept the GPLv3 license
- Do not add dependencies with incompatible licenses (e.g., GPL, AGPL without explicit permission)
- Document any new third-party libraries in this file
- Include proper attribution and license headers in new files
- Respect the original licenses of incorporated libraries

---

**Last Updated**: 2026-01-28
