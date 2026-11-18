# Shared functions of the E5 device gates (device-net, device-life, device-offline, soak).
# Ports of v1 scripts/gates/lib.sh for the dev package. Source after ../env.sh and lib.sh.
# Only the dev package io.github.z0orb.arelwars.emudev. The gates never install, uninstall or change v1.
P() { node -p "require('./scripts/gates/params.json').$1"; }
play() { bash scripts/play "$@"; }
TS=docs/saves/testsave
apk_of() { echo "dist/arelemu-$1.apk"; }

# Install a variant with adb install -r (TASK-E5.4: extra adb flags, for example --abi armeabi-v7a).
# HyperOS asks the owner to accept each install (standing OK, CLAUDE.md). INSTALL_FAILED_USER_RESTRICTED
# means that nobody accepted it: wait, then try again (max 4 tries).
install_v() {
  local v=$1; shift
  local apk; apk=$(apk_of "$v")
  [ -f "$apk" ] || { echo "$apk not built"; return 1; }
  [ "$(sha "$apk")" = "$(cat "$apk.sha256")" ] || { echo "$apk does not match its .sha256"; return 1; }
  local try out rc
  for try in 1 2 3 4; do
    out=$(adbs install -r "$@" "$apk" 2>&1 </dev/null); rc=$?
    echo "$out" >>"$EV/install-$v.txt"
    [ $rc = 0 ] && grep -q Success <<<"$out" && return 0
    if grep -q INSTALL_FAILED_USER_RESTRICTED <<<"$out"; then
      echo "install of $v try $try: nobody accepted the HyperOS prompt on the phone. Owner: please accept it. Next try in 30 s"
      sleep 30; continue
    fi
    echo "$out"; return 1
  done
  return 1
}
# Install the variant only if the installed APK is another one.
ensure_v() {
  [ "$(bash scripts/app installed-sha)" = "$(sha "$(apk_of "$1")")" ] && return 0
  install_v "$@"
}
installed_v() { # the variant that is installed: debug, release or other
  local s; s=$(bash scripts/app installed-sha)
  if [ "$s" = "$(sha "$(apk_of debug)")" ]; then echo debug
  elif [ "$s" = "$(sha "$(apk_of release)")" ]; then echo release
  else echo other; fi
}
abi() { adbs shell dumpsys package $DEV_PKG </dev/null | tr -d '\r' | sed -n 's/.*primaryCpuAbi=\([^ ]*\).*/\1/p' | head -1; }

wake() {
  local i
  for i in 1 2 3; do
    adbs shell input keyevent WAKEUP </dev/null; adbs shell wm dismiss-keyguard </dev/null >/dev/null 2>&1; sleep 1
    adbs shell dumpsys activity activities </dev/null | tr -d '\r' | grep -q 'mKeyguardShowing=true' || return 0
    # API 21 and 22 have no "wm dismiss-keyguard". MENU (82) opens a swipe keyguard (AVD, E6).
    adbs shell input keyevent 82 </dev/null
    sleep 2
  done
  return 1
}
launch() {
  wake
  bash scripts/app stop
  bash scripts/app launch
  sleep "$(P e5_launch_wait_sec)"
}
shot() { local f=$EV/$1.png; bash scripts/app shot "$f"; echo "$f"; }
changed() { node tools/screen.mjs pixels-changed "$1" "$2"; }
at_most() { node -e "process.exit(Number(process.argv[1]) <= Number(process.argv[2]) ? 0 : 1)" "$1" "$2"; }
at_least() { node -e "process.exit(Number(process.argv[1]) >= Number(process.argv[2]) ? 0 : 1)" "$1" "$2"; }
app_pid() { bash scripts/app pid; }

# The game box of ArelInsets (the box that play steps used last).
box() {
  local bl; bl=$(adbs logcat -d -s ArelInsets:I </dev/null | tr -d '\r' | grep ' box ' | tail -1 | sed 's/.* box /box /' || true)
  [ -n "$bl" ] || bl=$(cat work/arel-box-$SERIAL.txt 2>/dev/null || echo "box 372 0 1620 1080")
  set -- $bl; echo "$2 $3 $4 $5"
}
# region SHOT KEY: compare the region of params KEY (e5_battle and others) with its v1 golden. Prints the
# fraction that differs. Exit 0 when it is at most the max of KEY.
region() {
  local s=$1 key=$2 g reg max map c
  g="$(P e5_golden_dir)/$(P "$key.golden")"; reg=$(P "$key.region.join(' ')"); max=$(P "$key.max"); map=$(P "$key.map")
  local gbox; gbox=$(node -p "(require('./scripts/gates/params.json').$key.golden_box || [372, 0, 1620, 1080]).join(' ')")
  if [ "$map" = true ]; then c=$(node tools/screen.mjs region-changed "$g" "$s" $reg $(box) $gbox)
  else c=$(node tools/screen.mjs region-changed "$g" "$s" $reg 372 0 1620 1080); fi
  echo "$c" >"$s.$key.change"; echo "$c"
  at_most "$c" "$max"
}

# Device checks after each part (CC-5): the installed APK is the build of the variant, our activity has
# the focus, and the app logcat has no crash line.
DEVRUN=0
dev_checks() {
  local v=$1 hits
  DEVRUN=$((DEVRUN + 1))
  check "CC-5 $v: the installed APK equals $(apk_of "$v")" test "$(bash scripts/app installed-sha)" = "$(sha "$(apk_of "$v")")"
  check "CC-5 $v: our activity has the focus" play focused
  bash scripts/app logcat "$EV/logcat-$v-$DEVRUN.txt" || true
  # E6 session 3: an empty app logcat passed the crash check on API 21 to 30. The check needs app lines.
  n=$(grep -cv '^--------- ' "$EV/logcat-$v-$DEVRUN.txt" || true)
  check "CC-5 $v: the app logcat has app lines ($n, min 20)" test "${n:-0}" -ge 20
  hits=$EV/logcat-hits-$v-$DEVRUN.txt
  grep -E "$(P e1_logcat_bad_regex)" "$EV/logcat-$v-$DEVRUN.txt" | grep -Ev "$(P e5_logcat_ignore_regex)" >"$hits"
  check "CC-5 $v: app logcat has no crash line ($(basename "$hits"))" test ! -s "$hits"
}

# Hash of the whole save set. Debug: the files folder (run-as). Release: the save mirror of v1 patch
# ArelSaveMirror in the external files folder, as v1 scripts/pull-saves reads it.
set_hash() { (cd "$1" && ls Save*.dat Sng*.dat option.sav >/dev/null 2>&1 && sha256sum -b Save*.dat Sng*.dat option.sav 2>/dev/null | sha256sum | cut -d' ' -f1); }
save_hash() {
  local v=$1 d=$EV/save-now
  rm -rf "$d"; mkdir -p "$d"
  if [ "$v" = debug ]; then play saves-get "$d" >/dev/null 2>&1
  else adbs pull "/sdcard/Android/data/$DEV_PKG/files/saves/." "$d" </dev/null >/dev/null 2>&1; fi
  set_hash "$d"
}

# Saves of the dev package: back up, then put the test save in place (needs the debug build for run-as).
# SAVED is the backup folder, or empty when there were no saves.
swap_in_test_save() {
  SAVED=""
  ensure_v debug || { fail "debug installed for the save swap"; return 1; }
  local d=$EV/phone-saves; mkdir -p "$d"; play saves-get "$d"
  if [ -s "$d/option.sav" ]; then SAVED=$d; pass "dev package saves backed up to emu/$d"; else echo "no saves of the dev package to back up"; fi
  play saves-put $TS && pass "test save put in place" || { fail "test save put in place"; return 1; }
}
put_back_saves() { # then install the variant $1
  bash scripts/app stop
  ensure_v debug
  if [ -n "${SAVED:-}" ]; then play saves-put "$SAVED" && pass "dev package saves put back" || fail "dev package saves put back ($SAVED)"
  else play saves-clear && pass "test save deleted (there were no saves before)" || fail "test save deleted"; fi
  [ -n "${1:-}" ] && ensure_v "$1"
  return 0
}
# Put the test save in place for a release run: put it in with debug, then install release (-r keeps data).
testsave_for() {
  ensure_v debug && play saves-put $TS && ensure_v "$@"
}

# gate_start, then a record of what the gate tests: its arguments and the hashes of both builds.
# gate-E5 accepts the evidence of a device gate only for the builds in dist/ now (GC-E5.2, 5, 7, 9).
dev_start() {
  local name=$1; shift
  gate_start "$name"
  { echo "args $*"; echo "debug $(sha "$(apk_of debug)")"; echo "release $(sha "$(apk_of release)")"; } >"$EV/apks.txt"
}
