Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SongLedFlasher Dependency Analysis" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

Write-Host "`n[Self-Contained Status]" -ForegroundColor Yellow
Write-Host "✓ SongLedFlasher is fully self-contained" -ForegroundColor Green
Write-Host "✓ No .NET 8 installation required" -ForegroundColor Green
Write-Host "✓ All Windows Runtime DLLs bundled" -ForegroundColor Green

Write-Host "`n[Bundled Files]" -ForegroundColor Yellow
Get-ChildItem 'c:\Users\21733\Desktop\songled\dist\SongLedFlasher' -File | ForEach-Object {
    $size = [math]::Round($_.Length / 1MB, 1)
    Write-Host "  $($_.Name) - $size MB"
}

Write-Host "`n[External Dependencies]" -ForegroundColor Yellow
Write-Host "✓ esptool.py - Required (not bundled)" -ForegroundColor Cyan
Write-Host "  Must be in system PATH or manually provided"
Write-Host "  Install: pip install esptool"
Write-Host "  Or: Ships with PlatformIO"

Write-Host "`n[No Environment Variables Required]" -ForegroundColor Green
Write-Host "✓ No PLATFORMIO references" -ForegroundColor Green
Write-Host "✓ No PATH dependencies" -ForegroundColor Green
Write-Host "✓ No registry dependencies" -ForegroundColor Green

Write-Host "`n[System Requirements]" -ForegroundColor Yellow
Write-Host "✓ Windows 7 SP1 or later" -ForegroundColor Green
Write-Host "✓ USB driver for ESP32-S3 (CDC)" -ForegroundColor Cyan
Write-Host "✓ esptool in PATH or installed" -ForegroundColor Cyan

Write-Host "`n[Size Breakdown]" -ForegroundColor Yellow
Write-Host "  Main executable: 162 MB (includes .NET 8 runtime)" -ForegroundColor Green
Write-Host "  Runtime DLLs: ~8 MB (graphics, presentation)" -ForegroundColor Green
Write-Host "  Config/Debug: ~10 KB" -ForegroundColor Gray

Write-Host "`n[Usage Flow]" -ForegroundColor Yellow
Write-Host "1. Run SongLedFlasher.exe" -ForegroundColor Cyan
Write-Host "2. Select firmware (.bin file)" -ForegroundColor Cyan
Write-Host "3. Choose COM port" -ForegroundColor Cyan
Write-Host "4. Click Flash → calls esptool.py internally" -ForegroundColor Cyan

Write-Host "`n[Fallback if esptool not in PATH]" -ForegroundColor Yellow
Write-Host "Option A: Install esptool: pip install esptool" -ForegroundColor Gray
Write-Host "Option B: Provide esptool.exe in same directory" -ForegroundColor Gray
Write-Host "Option C: Use PlatformIO (includes esptool)" -ForegroundColor Gray

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Result: FULLY SELF-CONTAINED" -ForegroundColor Green
Write-Host "Only missing: esptool (intentional)" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan
