#!/bin/bash
# Clean up build artifacts and unnecessary files before pushing to GitHub

echo "🧹 Cleaning up SongLed project..."

# Remove PlatformIO artifacts
echo "Removing PlatformIO build artifacts..."
rm -rf .pio/
rm -rf build/

# Remove CMake artifacts
echo "Removing CMake build artifacts..."
rm -rf pc/SongLedPcCpp/build/
rm -rf cmake-build-debug/
rm -rf cmake-build-release/

# Remove C# build artifacts
echo "Removing C# build artifacts..."
rm -rf pc/SongLedPc/bin/
rm -rf pc/SongLedPc/obj/
rm -rf pc/SongLedPcCpp/bin/
rm -rf pc/SongLedPcCpp/obj/

# Remove logs
echo "Removing log files..."
find . -name "*.log" -delete
find . -name ".DS_Store" -delete
find . -name "Thumbs.db" -delete

# Remove IDE cache
echo "Removing IDE cache files..."
rm -rf .vscode/ipch/
rm -rf .idea/

# Remove Python cache
echo "Removing Python cache..."
find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true

echo "✅ Cleanup complete!"
echo ""
echo "Files to commit:"
echo "- src/"
echo "- pc/"
echo "- third_party/"
echo "- partitions/"
echo "- docs/"
echo "- README.md"
echo "- LICENSE"
echo "- THIRD_PARTY.md"
echo "- platformio.ini"
echo "- CMakeLists.txt"
echo "- .gitignore"
echo ""
echo "Ready to push to https://github.com/CoversiteTT/SongLed"
