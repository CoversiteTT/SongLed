#!/bin/bash
# Quick GitHub release push script for SongLed

set -e

echo "================================"
echo "SongLed GitHub Release Script"
echo "================================"
echo ""

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Step 1: Cleanup
echo -e "${BLUE}[1/4] Cleaning build artifacts...${NC}"
if [[ -d ".pio" ]]; then rm -rf .pio; fi
if [[ -d "build" ]]; then rm -rf build; fi
if [[ -d "pc/SongLedPc/bin" ]]; then rm -rf pc/SongLedPc/bin; fi
if [[ -d "pc/SongLedPc/obj" ]]; then rm -rf pc/SongLedPc/obj; fi
if [[ -d "pc/SongLedPcCpp/build" ]]; then rm -rf pc/SongLedPcCpp/build; fi
if [[ -d "pc/SongLedPcCpp/bin" ]]; then rm -rf pc/SongLedPcCpp/bin; fi
if [[ -d "pc/SongLedPcCpp/obj" ]]; then rm -rf pc/SongLedPcCpp/obj; fi
find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
find . -name "*.log" -delete 2>/dev/null || true
echo -e "${GREEN}✓ Cleanup complete${NC}"
echo ""

# Step 2: Verify files
echo -e "${BLUE}[2/4] Verifying required files...${NC}"
files=("LICENSE" "README.md" "THIRD_PARTY.md" "RELEASE_CHECKLIST.md" "src/" "pc/" "third_party/")
for file in "${files[@]}"; do
    if [[ -e "$file" ]]; then
        echo -e "${GREEN}✓ $file${NC}"
    else
        echo -e "${YELLOW}⚠ $file not found${NC}"
    fi
done
echo ""

# Step 3: Git status
echo -e "${BLUE}[3/4] Git status:${NC}"
git status --short | head -20
echo ""

# Step 4: Confirmation
echo -e "${YELLOW}Ready to push to GitHub?${NC}"
echo "Repository: https://github.com/CoversiteTT/SongLed"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 1
fi

# Step 5: Add and commit
echo ""
echo -e "${BLUE}[4/4] Committing changes...${NC}"
git add .
git commit -m "Initial public release with proper licensing and third-party attribution" \
           -m "- Add GPLv3 LICENSE file
- Add THIRD_PARTY.md with complete library documentation
- Update README with license and dependencies
- Add RELEASE_CHECKLIST.md for future contributors
- Add cleanup scripts for build artifacts
- Update .gitignore for proper file exclusion
- Identify and document oled-ui-astra, U8G2, and ZPIX Font usage"

echo -e "${GREEN}✓ Changes committed${NC}"
echo ""

# Step 6: Push
read -p "Push to GitHub? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Skipped push. Commit is ready locally."
    exit 0
fi

git push -u origin main

echo ""
echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}✓ Released to GitHub!${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo "Next steps:"
echo "1. Visit: https://github.com/CoversiteTT/SongLed"
echo "2. Set repository description:"
echo "   'ESP32-S3 Volume Knob for Windows 11 with display and lyrics'"
echo "3. Add topics: esp32, embedded-systems, audio, windows"
echo "4. Check LICENSE is set to GPLv3"
echo ""
