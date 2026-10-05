#!/bin/zsh
# 构建并安装到 /Applications，然后启动
set -euo pipefail
cd "$(dirname "$0")"

APP_NAME="MonitorSwitch.app"
SRC="build/$APP_NAME"
DEST="/Applications/$APP_NAME"

# 始终重新构建，避免 build/ 里残留旧产物时跳过更新
./build.sh

pkill -x MonitorSwitch 2>/dev/null || true
sleep 1

# 覆盖旧版本（已运行实例先退场，避免两份同时挂菜单栏）
rm -rf "$DEST"
ditto "$SRC" "$DEST"

open "$DEST"
echo "✅ 已安装并启动: $DEST"
