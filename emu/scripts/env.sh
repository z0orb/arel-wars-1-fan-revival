# Shared setup for emulator path scripts (PLAN.md 8.1). Source it; it moves to emu/.
# emu/ scripts never write outside emu/. v1 files (../original, ../dist, ../patches, ../scripts/local.env)
# and universal/ are read-only inputs.
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "${BASH_SOURCE[0]}")/.."
E=$PWD

LAD="$(cygpath -u "$LOCALAPPDATA")"
SDK="$LAD/Android/Sdk"
JAVA_BIN=""; SERIAL=""
# Machine-specific values (JDK folder, phone serial), shared with v1 and read only.
[ -f ../scripts/local.env ] && source ../scripts/local.env
# E6: AREL_SERIAL selects another device (an AVD, for example emulator-5554) for adbs and the gates.
PHONE_SERIAL=$SERIAL
if [ -n "${AREL_SERIAL:-}" ]; then SERIAL=$AREL_SERIAL; fi
NDK_VERSION=27.3.13750724
NDK="$SDK/ndk/$NDK_VERSION"
TC="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
export PATH="${JAVA_BIN:+$JAVA_BIN:}$SDK/build-tools/36.0.0:$SDK/platform-tools:$PATH"

DEV_PKG=io.github.z0orb.arelwars.emudev
PROBE_PKG=io.github.z0orb.arelwars.emuprobe
DEV_KEY=keys/dev.jks
DEV_KEY_PASS=keys/dev.pass
DEV_KEY_ALIAS=dev
V1_RELEASE_APK=../dist/arelwars-release.apk
ORIGINAL=../original/00293-Arel-Wars-v1-0-2.apk
ORIGINAL_SHA=1b099ef95c359836d080785dd45d741ccb17cc5a40d744854071df6dd2525a17
# Phone folder of the emulator path (PLAN.md 8.7). Never /data/local/tmp/arel-oracle.
PHONE_DIR=/data/local/tmp/arel-emu

sha() { sha256sum "$1" | cut -d' ' -f1; }
# E6: a frozen or lost AVD makes adb wait for ever. On an emulator serial, each adb call stops after
# AREL_ADB_TIMEOUT seconds (default 300) with exit 124. The phone keeps calls with no limit.
adbs() {
  case "$SERIAL" in
    emulator-*) timeout -k 10 "${AREL_ADB_TIMEOUT:-300}" adb -s "$SERIAL" "$@" ;;
    *) adb -s "$SERIAL" "$@" ;;
  esac
}
phone_up() { [ "$(adb -s "$SERIAL" get-state 2>/dev/null)" = device ]; }
# The dorm network and USB can drop. Retry a read-only adb command a few times before giving up.
adbr() {
  local i out
  for i in 1 2 3; do
    if out=$(adb -s "$SERIAL" "$@" 2>&1); then printf '%s\n' "$out"; return 0; fi
    sleep 3; timeout -k 10 120 adb -s "$SERIAL" wait-for-device >/dev/null 2>&1 || true
  done
  printf '%s\n' "$out"; return 1
}

# Sorted "sha256  path" list of every file under universal/ (CC-2).
universal_tree() { (cd .. && find universal -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum); }
