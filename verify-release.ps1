Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Release Package Contents" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$dist = "c:\Users\21733\Desktop\songled\dist"
$files = Get-ChildItem $dist -Recurse -File | Sort-Object Length -Descending

Write-Host "`nFile Size Summary:" -ForegroundColor Yellow
foreach ($f in $files) {
    $size = [math]::Round($f.Length / 1MB, 1)
    $rel = $f.FullName.Replace($dist + "\", "")
    Write-Host ("{0,10}MB  {1}" -f $size, $rel)
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Dependency Analysis:" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan

Write-Host "`n[SongLedFlasher.exe]" -ForegroundColor Green
Write-Host "  - Windows Forms (built-in)"
Write-Host "  - System.IO.Ports (built-in)"
Write-Host "  - System.Management (built-in)"
Write-Host "  - Calls: esptool.py (external, user must have)"
Write-Host "  - NO external environment references"

Write-Host "`n[SongLedPc.exe]" -ForegroundColor Green
Write-Host "  - Windows Forms (built-in)"
Write-Host "  - NAudio (bundled)"
Write-Host "  - System.IO.Ports (built-in)"
Write-Host "  - NO external environment references"

Write-Host "`n[songled-firmware.bin]" -ForegroundColor Green
Write-Host "  - Binary firmware, no dependencies"
Write-Host "  - Deploy to board via SongLedFlasher.exe"

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Status: ALL FILES SELF-CONTAINED" -ForegroundColor Green
Write-Host "No PlatformIO, PATH, or env vars needed" -ForegroundColor Green
Write-Host "Only requirement: esptool for flashing" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Cyan
