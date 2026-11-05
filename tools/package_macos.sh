#!/bin/zsh
# Build the game and assemble a double-clickable macOS .app bundle.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/dist/GTA Recomp.app"
BUILD="$ROOT/build"

cd "$ROOT"

cmake -S . -B "$BUILD" \
    -DGTA_DATA_DIR="$ROOT/game_data/GTADATA" \
    -DBUILD_TESTS=ON
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BUILD/dos_gta_recomp" "$APP/Contents/MacOS/dos_gta_recomp"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>GTA Recomp</string>
    <key>CFBundleExecutable</key>
    <string>dos_gta_recomp</string>
    <key>CFBundleIconFile</key>
    <string>icon.icns</string>
    <key>CFBundleIdentifier</key>
    <string>org.dosgta.recomp</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>GTA Recomp</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.action-games</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
PLIST

# Data is resolved from the absolute path baked into the binary; this symlink
# is a second fallback when the bundle is moved around the project.
ln -sfn "../../../../game_data/GTADATA" "$APP/Contents/Resources/GTADATA"

# Generate an icon from the current screenshot when available.
if [[ -f "$ROOT/docs/game.png" ]]; then
    ICONSET="$BUILD/icon.iconset"
    rm -rf "$ICONSET"
    mkdir -p "$ICONSET"
    sips -c 720 720 --cropOffset 0 280 "$ROOT/docs/game.png" \
        --out "$BUILD/icon_src.png" >/dev/null
    for size in 16 32 64 128 256 512; do
        sips -z "$size" "$size" "$BUILD/icon_src.png" \
            --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    done
    cp "$ICONSET/icon_32x32.png" "$ICONSET/icon_16x16@2x.png"
    cp "$ICONSET/icon_64x64.png" "$ICONSET/icon_32x32@2x.png"
    cp "$ICONSET/icon_256x256.png" "$ICONSET/icon_128x128@2x.png"
    cp "$ICONSET/icon_512x512.png" "$ICONSET/icon_256x256@2x.png"
    sips -z 1024 1024 "$BUILD/icon_src.png" \
        --out "$ICONSET/icon_512x512@2x.png" >/dev/null
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/icon.icns"
fi

ln -sfn "dist/GTA Recomp.app" "$ROOT/GTA Recomp.app"

echo
echo "Built: $APP"
echo "Double-click 'GTA Recomp.app' in the project root, or run:"
echo "  \"$APP/Contents/MacOS/dos_gta_recomp\""
