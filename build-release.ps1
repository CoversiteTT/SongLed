$ErrorActionPreference = "Stop"

$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Definition
$DIST = Join-Path $ROOT "dist"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SongLed Release Build" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if (Test-Path $DIST) { Remove-Item $DIST -Recurse -Force }
New-Item $DIST -ItemType Directory | Out-Null

# 1. Build Firmware
Write-Host "`n[1/3] Build Firmware (ESP32-S3)..." -ForegroundColor Yellow
Push-Location $ROOT
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --environment esp32s3
if ($LASTEXITCODE -ne 0) { throw "Firmware build failed" }
Write-Host "OK: Firmware built successfully" -ForegroundColor Green

# Copy firmware
if (Test-Path ".pio\build\esp32s3\firmware.bin") {
    Copy-Item ".pio\build\esp32s3\firmware.bin" "$DIST\songled-firmware.bin"
    Write-Host "OK: Firmware copied to dist" -ForegroundColor Green
}

# 2. Build Lite PC - Framework-dependent (lightweight)
Write-Host "`n[2/3] Build Lite PC App (SongLedPc)..." -ForegroundColor Yellow
Push-Location (Join-Path $ROOT "pc\SongLedPc")
& dotnet publish -c Release -r win-x64 --no-self-contained -p:PublishSingleFile=false -p:PublishTrimmed=false -o "$DIST\SongLedPc-lite" 2>&1 | Select-String -Pattern "error|already exists"
if ($LASTEXITCODE -ne 0) { throw "SongLedPc build failed" }

# Get just the main exe and config
Get-ChildItem "$DIST\SongLedPc-lite" -File | Where-Object { $_.Extension -ne ".exe" -and $_.Name -ne "SongLedPc.runtimeconfig.json" } | Remove-Item -Force
Write-Host "OK: Lite PC built successfully (framework-dependent)" -ForegroundColor Green

# 3. Build Flasher
Write-Host "`n[3/3] Build Flasher App (SongLedFlasher)..." -ForegroundColor Yellow
Push-Location (Join-Path $ROOT "pc\SongLedFlasher")
& dotnet publish -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -p:PublishTrimmed=false -o "$DIST\SongLedFlasher" 2>&1 | Select-String -Pattern "error|already exists"
if ($LASTEXITCODE -ne 0) { throw "SongLedFlasher build failed" }
Write-Host "OK: Flasher built successfully" -ForegroundColor Green

# Download and integrate esptool.exe
Write-Host "`n[3b] Downloading esptool.exe..." -ForegroundColor Yellow
$esptoolUrl = "https://github.com/espressif/esptool/releases/download/v4.7.0/esptool-v4.7.0-win64.zip"
$esptoolZip = "$DIST\esptool.zip"
$esptoolDir = "$DIST\SongLedFlasher\esptool_tmp"
$flasherDir = "$DIST\SongLedFlasher"

try
{
    # Download esptool
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $webClient = New-Object System.Net.WebClient
    Write-Host "Downloading..." -ForegroundColor Gray
    $webClient.DownloadFile($esptoolUrl, $esptoolZip)
    
    # Extract
    Write-Host "Extracting..." -ForegroundColor Gray
    $null = New-Item -ItemType Directory -Path $esptoolDir -Force
    Expand-Archive -Path $esptoolZip -DestinationPath $esptoolDir -Force -ErrorAction SilentlyContinue
    
    # Find and move esptool.exe
    $extracted = Get-ChildItem -Path $esptoolDir -Filter "esptool.exe" -Recurse | Select-Object -First 1
    if ($extracted)
    {
        Copy-Item -Path $extracted.FullName -Destination "$flasherDir\esptool.exe" -Force
        Write-Host "OK: esptool.exe integrated" -ForegroundColor Green
    }
    
    # Clean up
    Remove-Item $esptoolZip -Force -ErrorAction SilentlyContinue
    Remove-Item $esptoolDir -Recurse -Force -ErrorAction SilentlyContinue
}
catch
{
    Write-Host "Warning: esptool.exe download failed" -ForegroundColor Yellow
}

# Create README
Write-Host "`n[Final] Creating README..." -ForegroundColor Yellow
$readme = @"
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
✓ No installation required
✓ No environment variables
✓ No PATH configuration
✓ Everything included

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
"@

$readme | Out-File "$DIST\README.txt" -Encoding UTF8
Write-Host "OK: README created" -ForegroundColor Green

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "BUILD COMPLETE!" -ForegroundColor Green
Write-Host "Output: $DIST" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

Pop-Location
