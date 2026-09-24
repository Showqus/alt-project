#!/usr/bin/env bash
# End-to-end test of BedrockQoLLauncher.exe under Wine: injection, commands, notifications and
# auto-update from a local HTTP server that plays the role of the GitHub release.
#
#   tests/launcher_test.sh <build-dir> <dll-version-test-1> <dll-version-test-2>
#
# <build-dir> must contain BedrockQoLLauncher.exe and fake_game.exe (MinGW build).
set -u

BUILD=$(realpath "$1")
V1=$(realpath "$2")
V2=$(realpath "$3")
WINE=${WINE:-/usr/lib/wine/wine64}
WINESERVER=${WINESERVER:-$(dirname "$WINE")/wineserver}
PORT=${PORT:-8765}

WORK=$(mktemp -d)
export WINEDEBUG=-all
export WINEPREFIX="$WORK/prefix"
# The update server is local: keep Wine's WinHTTP away from any proxy.
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

FAILED=0
check() {
    if eval "$2"; then echo "[PASS] $1"; else echo "[FAIL] $1"; FAILED=1; fi
}
wait_for() {
    local condition=$1 seconds=${2:-20}
    for _ in $(seq $((seconds * 10))); do
        eval "$condition" && return 0
        sleep 0.1
    done
    return 1
}

APP="$WORK/app"
SERVER="$WORK/server"
mkdir -p "$APP" "$SERVER"
cp "$BUILD/BedrockQoLLauncher.exe" "$APP/"
cp "$V1" "$APP/BedrockQoL.dll"                     # the build shipped next to the launcher
cp "$BUILD/fake_game.exe" "$APP/Minecraft.Windows.exe"

"$WINE" wineboot -i >/dev/null 2>&1
USER_NAME=$(ls "$WINEPREFIX/drive_c/users" | grep -v -i public | head -1)
DATA="$WINEPREFIX/drive_c/users/$USER_NAME/Temp/BedrockQoL"
LOG="$APP/launcher.log"

cat > "$APP/launcher.ini" <<EOF
[Launcher]
AutoInject=1
InjectDelaySeconds=1
Tray=0
Notifications=1
[Update]
Enabled=1
ManifestUrl=http://127.0.0.1:$PORT/manifest.txt
CheckIntervalMinutes=60
[Game]
Process=Minecraft.Windows.exe
DataDir=C:\\users\\$USER_NAME\\Temp\\BedrockQoL
EOF

publish() {  # publish <version> <dll> [sha256 override]
    cp "$2" "$SERVER/BedrockQoL.dll"
    local sha=${3:-$(sha256sum "$2" | cut -d' ' -f1)}
    printf 'version=%s\nfile=BedrockQoL.dll\nsha256=%s\n' "$1" "$sha" > "$SERVER/manifest.txt"
}
status_version() { grep -s '^version=' "$DATA/control/status.txt" | cut -d= -f2; }
launcher() { (cd "$APP" && "$WINE" BedrockQoLLauncher.exe "$@" >/dev/null 2>&1); }

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$SERVER" >/dev/null 2>&1 &
SERVER_PID=$!
cleanup() {
    "$WINESERVER" -k 2>/dev/null
    kill "$SERVER_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT
publish test-1 "$V1"

start_bg() { (cd "$APP" && exec "$WINE" "$@") >/dev/null 2>&1 & }
start_bg Minecraft.Windows.exe --host 600
sleep 2

# 1. One-shot injection of the bundled DLL.
launcher --inject
check "--inject loads the bundled DLL into the game" "wait_for '[ \"\$(status_version)\" = test-1 ]'"
check "launcher.log records the injection" "grep -q 'Injected' '$LOG'"

# 2. Commands from outside the game.
launcher --cmd toggle fullbright
check "--cmd reaches the mod" "wait_for 'grep -q \"Fullbright: ВКЛ\" \"$DATA/control/notifications.txt\"'"

# 3. First update check: nothing installed yet -> download test-1 and swap the running copy.
launcher --check-update
check "update downloads test-1" "grep -q '^version=test-1' '$APP/dll/installed.txt'"
check "downloaded DLL is loaded into the game" "wait_for 'grep -q \"Injected .*BedrockQoL-test-1.dll\" \"$LOG\"'"

# 4. New build published -> launcher downloads it, unloads the old DLL and injects the new one.
publish test-2 "$V2"
launcher --check-update
check "update to test-2 is downloaded" "grep -q '^version=test-2' '$APP/dll/installed.txt'"
check "game now runs test-2" "wait_for '[ \"\$(status_version)\" = test-2 ]'"
check "old DLL file removed after the swap" "[ ! -e '$APP/dll/BedrockQoL-test-1.dll' ]"

# 5. Nothing new -> nothing happens.
launcher --check-update
check "no re-download when already up to date" "grep -q 'test-2' '$LOG' && tail -3 '$LOG' | grep -q 'последняя версия'"

# 6. Tampered download is rejected.
publish test-3 "$V1" 0000000000000000000000000000000000000000000000000000000000000000
launcher --check-update
check "DLL with a wrong SHA-256 is rejected" "tail -3 '$LOG' | grep -q 'SHA-256' && grep -q '^version=test-2' '$APP/dll/installed.txt'"
publish test-2 "$V2"

# 7. Background (headless) mode: auto-inject into a freshly started game and forward notifications.
"$WINESERVER" -k 2>/dev/null
sleep 1
rm -f "$DATA/control/status.txt"
start_bg Minecraft.Windows.exe --host 600
start_bg BedrockQoLLauncher.exe --no-tray
check "background launcher auto-injects the installed build" "wait_for '[ \"\$(status_version)\" = test-2 ]' 30"
launcher --cmd toggle zoom
check "mod notifications are forwarded by the launcher" "wait_for 'grep -q \"Notify: Zoom: ВЫКЛ\" \"$LOG\"'"

echo
if [ "$FAILED" = 0 ]; then echo "LAUNCHER TEST PASSED"; else echo "LAUNCHER TEST FAILED"; echo "--- launcher.log"; cat "$LOG"; fi
exit "$FAILED"
