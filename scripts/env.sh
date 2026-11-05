# Shared setup. Source this from every script; it moves to the project root.
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "${BASH_SOURCE[0]}")/.."

LAD="$(cygpath -u "$LOCALAPPDATA")"
JAVA_BIN=""; SERIAL=""
# Machine-specific values (JDK folder, phone serial) live in scripts/local.env, which is never published.
[ -f scripts/local.env ] && source scripts/local.env
export PATH="${JAVA_BIN:+$JAVA_BIN:}$LAD/Android/Sdk/build-tools/36.0.0:$LAD/Android/Sdk/platform-tools:$PATH"

ORIGINAL=original/00293-Arel-Wars-v1-0-2.apk
ORIGINAL_SHA=1b099ef95c359836d080785dd45d741ccb17cc5a40d744854071df6dd2525a17
APKTOOL=tools/apktool/apktool_3.0.3.jar
PKG=com.gamevil.eruelwars.global

JDK25=tools/jdk-25/jdk-25.0.5+8   # Ghidra 12.2 needs JDK 25; everything else uses JDK 21 from PATH

sha() { sha256sum "$1" | cut -d' ' -f1; }
adbs() { adb -s "$SERIAL" "$@"; }
apk_path() { echo "dist/arelwars-${1:-release}.apk"; }
