# Shared emulator path gate functions (PLAN.md 8.3, 8.4). Source after ../env.sh.
# Gates count failures instead of stopping at the first one, so -e and pipefail are off here.
set +e +o pipefail
source scripts/v1check.sh
GATE_FAILS=0
gate_start() {
  GATE=$1
  EV=logs/gate-$GATE-$(date +%Y%m%d-%H%M%S)
  mkdir -p "$EV"
  exec > >(tee -a "$EV/output.txt") 2>&1
  echo "gate-$GATE evidence: emu/$EV"
}
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; GATE_FAILS=$((GATE_FAILS + 1)); }
check() { local name=$1; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }
# A task that is not done yet. It always fails, so a gate cannot pass early (PLAN.md 8.4).
todo() { fail "$1 (not done)"; }
gate_end() {
  if [ "$GATE_FAILS" = 0 ]; then echo "GATE-$GATE PASS"; sleep 1; exit 0; fi
  echo "GATE-$GATE FAIL ($GATE_FAILS failed)"; sleep 1; exit 1
}

# CC-1: v1 untouched. CC-2: universal/ untouched. CC-3: no emu script names the v1 package.
# CC-4: git tracks only the public set. CC-6: gate hashes match, and HANDOFF.md holds the SHA256SUMS hash.
common_checks() {
  v1_tree >"$EV/v1-tree.txt"
  check "CC-1 v1 scripts/ patches/ dist/ keys/ equal docs/V1-BASELINE.txt" diff <(grep -v '^#' docs/V1-BASELINE.txt) "$EV/v1-tree.txt"
  if phone_up; then
    local got; got=$(v1_installed_sha); echo "$got" >"$EV/v1-installed-sha.txt"
    check "CC-1 installed v1 APK hash is $V1_APK_SHA" test "$got" = "$V1_APK_SHA"
  else
    echo "SKIP  CC-1 installed v1 APK hash (phone $SERIAL not connected)"
  fi
  universal_tree >"$EV/universal-tree.txt"
  check "CC-2 universal/ equals docs/UNIVERSAL-BASELINE.txt" diff <(grep -v '^#' docs/UNIVERSAL-BASELINE.txt) "$EV/universal-tree.txt"
  # Built from parts so this file does not match.
  local p="com.gamevil.eruel""wars.global"
  grep -rlF "$p" scripts tools bridge runner shell probe ref-oracle 2>/dev/null | grep -vx 'scripts/v1check.sh' >"$EV/pkg-name-hits.txt"
  check "CC-3 no emu script except scripts/v1check.sh names the v1 package" test ! -s "$EV/pkg-name-hits.txt"
  (cd .. && git ls-files | LC_ALL=C sort) >"$EV/git-ls-files.txt"
  check "CC-4 git ls-files equals scripts/public-files.txt" cmp -s "$EV/git-ls-files.txt" ../scripts/public-files.txt
  check "CC-6 gate script hashes match SHA256SUMS" bash -c 'cd scripts/gates && sha256sum -c --quiet SHA256SUMS'
  local sums; sums=$(sha scripts/gates/SHA256SUMS)
  check "CC-6 SHA256SUMS hash $sums is in HANDOFF.md" grep -qF "$sums" ../HANDOFF.md
}

# CC-7, from E4: no decompiler artefacts in our sources.
artefact_check() {
  grep -rnE 'FUN_|DAT_|undefined4|param_|local_' bridge/src shell/src >"$EV/artefacts.txt" 2>/dev/null
  check "CC-7 no decompiler artefacts in bridge/src and shell/src" test ! -s "$EV/artefacts.txt"
}
