#!/bin/zsh
# 构建 MonitorSwitch.app（本机自用，ad-hoc 签名）
set -euo pipefail
cd "$(dirname "$0")"

APP=build/MonitorSwitch.app
rm -rf build
mkdir -p "$APP/Contents/MacOS"

swiftc -O -swift-version 5 -parse-as-library \
  -target arm64-apple-macos13.0 \
  MonitorSwitch.swift \
  -o "$APP/Contents/MacOS/MonitorSwitch"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>MonitorSwitch</string>
    <key>CFBundleDisplayName</key><string>MonitorSwitch</string>
    <key>CFBundleIdentifier</key><string>com.wuhan.monitorswitch</string>
    <key>CFBundleExecutable</key><string>MonitorSwitch</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>LSUIInterface</key><true/>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

# 图标（首次请先跑 ./make_icon.sh 生成 Assets/AppIcon.icns 与 Assets/MenuBarMI.png）
if [ -f Assets/AppIcon.icns ]; then
  mkdir -p "$APP/Contents/Resources"
  cp Assets/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
fi
if [ -f Assets/MenuBarMI.png ]; then
  mkdir -p "$APP/Contents/Resources"
  cp Assets/MenuBarMI.png "$APP/Contents/Resources/MenuBarMI.png"
fi

# 寄存器工具（背光用，来自 Mimonitor_Toolbox，MIT）
if [ -f Assets/MtkDirectTool.jar ]; then
  mkdir -p "$APP/Contents/Resources"
  cp Assets/MtkDirectTool.jar "$APP/Contents/Resources/MtkDirectTool.jar"
fi

codesign --force --sign - "$APP"
echo "✅ 构建完成: $PWD/$APP"
