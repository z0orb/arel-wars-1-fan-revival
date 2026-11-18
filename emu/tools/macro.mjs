// Turns v1 device macros (params.json steps: "tap X Y" in phone pixels, "sleep S") into oracle
// script lines on the 47 ms frame clock. Phone x 372..1992 is the game's 480 px (3.375 px each);
// Java converts with integer division, so game x = floor((x - 372) * 480 / 1620), y = floor(y * 320 / 1080).
// Usage: node tools/oracle/macro.mjs START_FRAME step [step ...]   (prints lines and "# next N")
// emu copy: also "dtap X Y" (a double tap, DTAP_GAP frames apart; the device sends two taps in one adb shell).
const DTAP_GAP = Number(process.env.DTAP_GAP || 8);
const toGame = (x, y) => [Math.floor(((x - 372) * 480) / 1620), Math.floor((y * 320) / 1080)];
export function convert(start, steps, tapGap = 2) {
  let f = start;
  const out = [];
  for (const s of steps) {
    const [cmd, a, b] = s.split(/\s+/);
    if (cmd === 'tap') { const [x, y] = toGame(+a, +b); out.push(`${f} tap ${x} ${y}`); f += tapGap; }
    else if (cmd === 'dtap') { const [x, y] = toGame(+a, +b); out.push(`${f} tap ${x} ${y}`, `${f + DTAP_GAP} tap ${x} ${y}`); f += DTAP_GAP + tapGap; }
    else if (cmd === 'sleep') f += Math.round((+a * 1000) / 47);
    else if (cmd === 'key' && a === 'BACK') { out.push(`${f} back`); f += tapGap; }
    else throw new Error(`step not handled: ${s}`);
  }
  return { lines: out, next: f };
}
if (process.argv[1]?.endsWith('macro.mjs')) {
  const [start, ...steps] = process.argv.slice(2);
  const r = convert(+start, steps);
  console.log(r.lines.join('\n'));
  console.log(`# next ${r.next}`);
}
