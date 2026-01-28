@echo off
REM Clean up build artifacts and unnecessary files before pushing to GitHub

echo 🧹 Cleaning up SongLed project...
echo.

REM Remove PlatformIO artifacts
echo Removing PlatformIO build artifacts...
if exist .pio (
    rmdir /s /q .pio >nul 2>&1
)
if exist build (
    rmdir /s /q build >nul 2>&1
)

REM Remove CMake artifacts
echo Removing CMake build artifacts...
if exist "pc\SongLedPcCpp\build" (
    rmdir /s /q "pc\SongLedPcCpp\build" >nul 2>&1
)
if exist cmake-build-debug (
    rmdir /s /q cmake-build-debug >nul 2>&1
)
if exist cmake-build-release (
    rmdir /s /q cmake-build-release >nul 2>&1
)

REM Remove C# build artifacts
echo Removing C# build artifacts...
if exist "pc\SongLedPc\bin" (
    rmdir /s /q "pc\SongLedPc\bin" >nul 2>&1
)
if exist "pc\SongLedPc\obj" (
    rmdir /s /q "pc\SongLedPc\obj" >nul 2>&1
)
if exist "pc\SongLedPcCpp\bin" (
    rmdir /s /q "pc\SongLedPcCpp\bin" >nul 2>&1
)
if exist "pc\SongLedPcCpp\obj" (
    rmdir /s /q "pc\SongLedPcCpp\obj" >nul 2>&1
)

REM Remove IDE cache
echo Removing IDE cache files...
if exist ".vscode\ipch" (
    rmdir /s /q ".vscode\ipch" >nul 2>&1
)
if exist ".idea" (
    rmdir /s /q ".idea" >nul 2>&1
)

REM Remove Python cache
echo Removing Python cache...
for /d /r . %%d in (__pycache__) do (
    if exist "%%d" (
        rmdir /s /q "%%d" >nul 2>&1
    )
)

echo.
echo ✅ Cleanup complete!
echo.
echo Files to commit:
echo   - src\
echo   - pc\
echo   - third_party\
echo   - partitions\
echo   - docs\
echo   - README.md
echo   - LICENSE
echo   - THIRD_PARTY.md
echo   - platformio.ini
echo   - CMakeLists.txt
echo   - .gitignore
echo.
echo Ready to push to https://github.com/CoversiteTT/SongLed
echo.
