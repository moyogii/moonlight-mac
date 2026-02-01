#!/bin/bash
#
# build-dmg.sh - Build Moonlight and create a distributable DMG
#
# This script mimics the GitHub Actions workflow for local testing.
#
# Usage:
#   ./scripts/build-dmg.sh              # Full build + DMG
#   ./scripts/build-dmg.sh --skip-build # Only sign and create DMG (use existing build)
#   ./scripts/build-dmg.sh --clean      # Clean build first
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_BUNDLE="$PROJECT_ROOT/app/Moonlight.app"

SKIP_BUILD=false
CLEAN_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build) SKIP_BUILD=true; shift ;;
        --clean) CLEAN_BUILD=true; shift ;;
        --help|-h)
            echo "Usage: $0 [--skip-build] [--clean] [--help]"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Check dependencies
if [ ! -f "$PROJECT_ROOT/moonlight-qt.pro" ]; then
    echo "Error: Run from moonlight-qt root or scripts folder"
    exit 1
fi

for cmd in qmake6 create-dmg codesign; do
    if ! command -v $cmd >/dev/null 2>&1; then
        echo "Error: $cmd not found"
        exit 1
    fi
done

# Build
if [ "$SKIP_BUILD" = false ]; then
    cd "$PROJECT_ROOT"
    [ "$CLEAN_BUILD" = true ] && { make clean 2>/dev/null || true; rm -rf "$APP_BUNDLE"; }

    echo "Building..."
    qmake6 moonlight-qt.pro
    make release
fi

if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: Moonlight.app not found"
    exit 1
fi

# Sign (matching workflow)
# Note: --options runtime is NOT used because it's incompatible with linker-signed
# libraries on macOS 15+ and causes "different Team IDs" errors at runtime
echo "Signing..."
find "$APP_BUNDLE/Contents/Frameworks" -type f \( -name "*.dylib" -o -name "*.framework" \) -exec codesign --remove-signature {} \; 2>/dev/null || true
codesign --remove-signature "$APP_BUNDLE/Contents/MacOS/Moonlight" 2>/dev/null || true
codesign --force --deep --sign - "$APP_BUNDLE"
xattr -cr "$APP_BUNDLE"

echo "Verifying..."
codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"

# Create DMG
VERSION=$(cat "$PROJECT_ROOT/app/version.txt" 2>/dev/null || echo "0.0.0")
DMG_NAME="Moonlight-v${VERSION}-macOS.dmg"

echo "Creating DMG..."
cd "$PROJECT_ROOT"
rm -f "$DMG_NAME" Moonlight.dmg

# create-dmg returns non-zero if it can't set a custom icon, which is fine
create-dmg \
    --volname "Moonlight v${VERSION}" \
    --window-pos 200 120 \
    --window-size 600 300 \
    --icon-size 100 \
    --icon "Moonlight.app" 175 120 \
    --hide-extension "Moonlight.app" \
    --app-drop-link 425 120 \
    "$DMG_NAME" \
    "app/Moonlight.app" \
    || true

if [ ! -f "$DMG_NAME" ]; then
    echo "Error: DMG creation failed"
    exit 1
fi

codesign --force --sign - "$DMG_NAME"
ln -sf "$DMG_NAME" Moonlight.dmg

echo ""
echo "Done: $PROJECT_ROOT/$DMG_NAME"
