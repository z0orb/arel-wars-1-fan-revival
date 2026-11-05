# Shared gate functions. Source after env.sh.
# Gates count failures instead of stopping at the first one, so -e is off here.
# pipefail is off too: grep -q closes its pipe early, which would turn a match into a failure.
set +e +o pipefail
GATE_FAILS=0
gate_start() {
  GATE=$1
  EV=logs/gate-$GATE-$(date +%Y%m%d-%H%M%S)
  mkdir -p "$EV"
  exec > >(tee -a "$EV/output.txt") 2>&1
  echo "gate-$GATE evidence: $EV"
}
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; GATE_FAILS=$((GATE_FAILS + 1)); }
gate_end() {
  if [ "$GATE_FAILS" = 0 ]; then echo "GATE-$GATE PASS"; sleep 1; exit 0; fi
  echo "GATE-$GATE FAIL ($GATE_FAILS failed)"; sleep 1; exit 1
}
H() { node scripts/gates/helpers.mjs "$@"; }
param() { H param "$1"; }

# Macro steps are strings: "tap X Y", "swipe X1 Y1 X2 Y2 MS", "key CODE", "sleep SECONDS".
run_steps() {
  local steps s
  steps=$(H steps "$1") || return 1
  # Never send taps to a lock screen: they could land on the PIN pad.
  if keyguard_up; then echo "lock screen is showing, not running $1"; return 1; fi
  # Its own fd: adb shell reads stdin, and would swallow the remaining steps.
  while IFS= read -r s <&3; do
    s=${s%$'\r'}
    set -- $s
    # Taps and swipes only ever go to our own app, never to whatever else is on screen.
    case "$1" in
      tap|swipe) if ! focused; then echo "our app is not focused, not sending: $s"; return 1; fi ;;
    esac
    case "$1" in
      tap) adbs shell input tap "$2" "$3" ;;
      swipe) adbs shell input swipe "$2" "$3" "$4" "$5" "$6" ;;
      key) adbs shell input keyevent "$2" ;;
      sleep) sleep "$2" ;;
      *) echo "unknown step: $s"; return 1 ;;
    esac
  done 3<<<"$steps"
}
shot() {
  local out=$EV/$1.png
  adbs shell screencap -p /data/local/tmp/arelwars-gate.png
  adbs pull /data/local/tmp/arelwars-gate.png "$out" >/dev/null 2>&1
  adbs shell rm -f /data/local/tmp/arelwars-gate.png
  echo "$out"
}
# Netstats only updates when polled. Without the poll, recent traffic is invisible (tested 2026-11-05).
netstats_dump() { adbs shell dumpsys netstats --poll >/dev/null; adbs shell dumpsys netstats detail | tr -d '\r' >"$1"; }
keyguard_up() { adbs shell dumpsys activity activities | tr -d '\r' | grep -q 'mKeyguardShowing=true'; }
secure_lock() { adbs shell dumpsys window | tr -d '\r' | grep -q 'secure=true'; }
focused() { adbs shell dumpsys window | tr -d '\r' | grep mCurrentFocus | head -1 | grep -Eq "$(param focus_regex)"; }
app_pid() { adbs shell pidof "$PKG" | tr -d '\r'; }
app_uid() { adbs shell dumpsys package "$PKG" | tr -d '\r' | sed -En 's/.*(userId|appId)=([0-9]+).*/\2/p' | head -1; }
wake() { adbs shell input keyevent WAKEUP; adbs shell wm dismiss-keyguard; }
install_variant() {
  local args=("$1")
  [ "$(param install_bypass)" = true ] && args+=(--bypass)
  bash scripts/install "${args[@]}" >"$EV/install-$1.txt" 2>&1
}
launch() {
  adbs logcat -c
  wake
  adbs shell am force-stop "$PKG"
  adbs shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
  sleep "$(param launch_wait_sec)"
  # Debuggable builds at a legacy target get an Android text-relocation dialog, which blocks the game
  # until dismissed. Phase 4 removes DT_TEXTREL and must set this macro back to null.
  if H steps macros.debug_after_launch >/dev/null 2>&1 && adbs shell dumpsys package "$PKG" | tr -d '' | grep -q 'pkgFlags=.* DEBUGGABLE'; then
    run_steps macros.debug_after_launch
  fi
}
# Common device checks from PLAN.md section 6.9.
common_checks() {
  local variant=$1 path focus hits
  COMMON_RUN=$((${COMMON_RUN:-0} + 1))
  path=$(adbs shell pm path "$PKG" | tr -d '\r' | sed -n 's/^package://p' | head -1)
  if [ -n "$path" ] && adbs pull "$path" "$EV/installed-$variant.apk" >/dev/null 2>&1 \
    && [ "$(sha "$EV/installed-$variant.apk")" = "$(cat "$(apk_path "$variant").sha256")" ]; then
    pass "$variant: installed APK hash equals build hash"
  else fail "$variant: installed APK hash equals build hash"; fi
  rm -f "$EV/installed-$variant.apk"
  focus=$(adbs shell dumpsys window | tr -d '\r' | grep mCurrentFocus | head -1)
  echo "$focus" >"$EV/focus-$variant.txt"
  if echo "$focus" | grep -Eq "$(param focus_regex)"; then pass "$variant: our activity has focus"; else fail "$variant: our activity has focus ($focus)"; fi
  adbs logcat -d >"$EV/logcat-$variant.txt"
  cp "$EV/logcat-$variant.txt" "$EV/logcat-$variant-$COMMON_RUN.txt"
  hits=$EV/logcat-hits-$variant-$COMMON_RUN.txt
  # logcat_ignore_regex lists phone system noise with recorded evidence (see HANDOFF.md).
  grep -E 'FATAL|JNI DETECTED|dlopen failed' "$EV/logcat-$variant.txt" | grep -Ev "$(param logcat_ignore_regex)" >"$hits"
  if [ -s "$hits" ]; then fail "$variant: logcat clean (see $(basename "$hits"))"; else pass "$variant: logcat clean"; fi
}
owner_confirmed() { grep -Eq "^$1: yes\b" docs/OWNER.md 2>/dev/null; }
