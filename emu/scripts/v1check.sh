# The only emu/ file that may name the v1 package (CC-3). Read-only checks; it never writes to v1.
# Source after env.sh.
V1_PKG=com.gamevil.eruelwars.global
V1_APK_SHA=2c79104b144f3c4b003fddcc9de6c2e986320ae78fd1abe8a58419a00998640d
V1_MIRROR=/sdcard/Android/data/$V1_PKG/files/saves

# Sorted "sha256  path" list of every file under v1's scripts/, patches/, dist/ and keys/.
v1_tree() { (cd .. && find scripts patches dist keys -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum); }

# Hash of the installed v1 APK, or a reason it can't be read. Pulls into emu/work only.
v1_installed_sha() {
  local path tmp=work/v1-installed.apk
  path=$(adbs shell pm path "$V1_PKG" | tr -d '\r' | sed -n 's/^package://p' | head -1)
  [ -n "$path" ] || { echo "not-installed"; return 1; }
  mkdir -p work
  adbs pull "$path" "$tmp" >/dev/null 2>&1 || { echo "pull-failed"; return 1; }
  sha "$tmp"; rm -f "$tmp"
}

# Hashes of the v1 save mirror files, read on the phone without changing anything.
v1_mirror_listing() { adbs shell "cd $V1_MIRROR && sha256sum *" | tr -d '\r'; }

# Copies the v1 save mirror into a folder under emu/ with adb pull (read only on the phone).
v1_mirror_pull() { mkdir -p "$1"; adbs pull "$V1_MIRROR/." "$1" >/dev/null; }
