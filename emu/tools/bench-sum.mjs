// Summary of emu/scripts/bench (PLAN.md TASK-E7.1, DES-16). Reads the ArelEmuPerf lines of each run
// (run-N-perf.txt) and the SurfaceFlinger latency dump (run-N-sf.txt), and writes the medians.
// Usage: node tools/bench-sum.mjs EVDIR RUNS
// A run uses every "perf" window of its battle part. fps = all intervals / all seconds. p50, p95 and max come
// from the summed 0.5 ms "hist" buckets of all windows (exact to 0.5 ms; the max is the largest window max).
// Logic: the game does one step for each NativeRender call, 16 steps for each game second
// (universal/spec/timing.md). So 22 game seconds take 22 * 16 / fps real seconds.
import fs from 'fs';
const [dir, runsArg] = process.argv.slice(2);
const p = JSON.parse(fs.readFileSync('scripts/gates/params.json', 'utf8'));
const runs = +runsArg;
const med = a => {
  const s = a.filter(x => !isNaN(x)).sort((x, y) => x - y);
  if (!s.length) return NaN;
  const h = s.length >> 1;
  return s.length % 2 ? s[h] : (s[h - 1] + s[h]) / 2;
};
const out = [];
const rows = [];
for (let r = 1; r <= runs; r++) {
  const f = `${dir}/run-${r}-perf.txt`;
  if (!fs.existsSync(f)) { out.push(`run ${r}: no perf file`); continue; }
  const lines = fs.readFileSync(f, 'utf8').split(/\r?\n/);
  let frames = 0, sec = 0, max = 0, renderSum = 0, windows = 0;
  const hist = new Map();
  let lastPerf = null;
  for (const l of lines) {
    let m = l.match(/perf total (\d+) frames (\d+) sec ([\d.]+) fps ([\d.]+) p50 ([\d.]+) p95 ([\d.]+) max ([\d.]+) render ([\d.]+)/);
    if (m) {
      lastPerf = { n: +m[2], sec: +m[3], max: +m[7], render: +m[8] };
      continue;
    }
    m = l.match(/ArelEmuPerf: hist (.*)$/) || l.match(/^hist (.*)$/);
    if (m && lastPerf) {
      // Use a window only with its histogram, so a log line lost at the end cannot count half a window.
      frames += lastPerf.n; sec += lastPerf.sec; renderSum += lastPerf.render * lastPerf.n; windows++;
      max = Math.max(max, lastPerf.max);
      for (const kv of m[1].trim().split(/ +/)) { const [k, c] = kv.split(':').map(Number); hist.set(k, (hist.get(k) || 0) + c); }
      lastPerf = null;
    }
  }
  const keys = [...hist.keys()].sort((a, b) => a - b);
  const total = keys.reduce((s, k) => s + hist.get(k), 0);
  const pct = q => { const want = Math.ceil(total * q); let c = 0; for (const k of keys) { c += hist.get(k); if (c >= want) return k / 2; } return NaN; };
  const fps = frames / sec;
  const row = { r, windows, frames, sec, fps, p50: pct(0.5), p95: pct(0.95), max, render: renderSum / frames,
    logic: (p.e7_game_sec * p.e7_game_frames_per_sec) / fps, sf: NaN };
  const sfFile = `${dir}/run-${r}-sf.txt`;
  if (fs.existsSync(sfFile)) {
    // dumpsys SurfaceFlinger --latency: line 1 the refresh period (ns), then "desired actual ready" (ns).
    const t = fs.readFileSync(sfFile, 'utf8').trim().split(/\r?\n/).slice(1).map(l => l.trim().split(/\s+/).map(Number))
      .filter(a => a.length === 3 && a[1] > 0 && a[1] < 9e18).map(a => a[1]);
    if (t.length > 10) row.sf = (t.length - 1) / ((t[t.length - 1] - t[0]) / 1e9);
  }
  rows.push(row);
  out.push(`run ${r}: windows ${windows} frames ${frames} sec ${sec.toFixed(1)} fps ${fps.toFixed(2)} p50 ${row.p50.toFixed(1)} p95 ${row.p95.toFixed(1)} max ${max.toFixed(1)} render ${row.render.toFixed(2)} ms, 22 game s = ${row.logic.toFixed(2)} real s, SurfaceFlinger fps ${isNaN(row.sf) ? 'n/a' : row.sf.toFixed(2)}`);
}
const M = k => med(rows.map(x => x[k]));
const sum = {
  runs: rows.length, fps: M('fps'), p50: M('p50'), p95: M('p95'), max: M('max'), render: M('render'),
  logic: M('logic'), sf: M('sf'), battleSec: Math.min(...rows.map(x => x.sec)),
};
out.push(`median of ${sum.runs}: fps ${sum.fps.toFixed(2)} p50 ${sum.p50.toFixed(1)} p95 ${sum.p95.toFixed(1)} max ${sum.max.toFixed(1)} render ${sum.render.toFixed(2)} ms, 22 game s = ${sum.logic.toFixed(2)} real s, SurfaceFlinger fps ${isNaN(sum.sf) ? 'n/a' : sum.sf.toFixed(2)}, shortest battle part ${sum.battleSec.toFixed(1)} s`);
out.push(`limits: fps >= ${p.e7_min_fps}, p95 <= ${p.e7_max_p95_ms} ms, 22 game s in ${p.e7_real_sec_range.join(' to ')} real s, battle part >= ${p.e7_min_battle_sec} s`);
out.push(`median ${JSON.stringify(sum)}`);
fs.writeFileSync(`${dir}/summary.txt`, out.join('\n') + '\n');
console.log(out.join('\n'));
