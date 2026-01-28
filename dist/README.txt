# SongLed Release Package

## Contents

- **songled-firmware.bin** - ESP32-S3 firmware (1 MB)
- **SongLedPc-lite/** - Lightweight PC application (152 KB, requires .NET 8)
- **SongLedFlasher/** - Firmware flasher application (175 MB, fully self-contained)

## Quick Start

### 1. Flash Firmware (First Time Only)
1. Open SongLedFlasher/SongLedFlasher.exe
2. Select firmware file: songled-firmware.bin
3. Choose COM port (ESP32-S3 serial port)
4. Click "Start Flash" - Done!

### 2. Run Application
Double-click SongLedPc-lite/SongLedPc.exe

If .NET 8 is missing, a dialog will prompt you with download link.

## Requirements
- Windows 7 SP1 or later
- USB driver for ESP32-S3 (CDC, usually auto-installed)
- .NET 8 runtime (for SongLedPc only, auto-detected)

## No External Dependencies!
鉁?No installation required
鉁?No environment variables
鉁?No PATH configuration
鉁?Everything included

## File Breakdown

| Component | Size | Notes |
|-----------|------|-------|
| Firmware | 1 MB | Binary for ESP32-S3 |
| SongLedPc.exe | 152 KB | Requires .NET 8 |
| SongLedFlasher.exe | 162 MB | Includes .NET 8 runtime |
| esptool.exe | 12 MB | Flashing tool (integrated) |
| **Total** | **175 MB** | Ready to use, no setup |

## Troubleshooting

### COM port not detected?
- Check USB cable is connected
- Install ESP32-S3 driver if needed
- Try different USB ports

### Flash fails with "permission denied"?
- Close SongLedPc if running
- Disconnect/reconnect the device
- Try again

## Support
For issues or questions, visit the project repository.
