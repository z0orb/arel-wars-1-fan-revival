// Guest-time statistics of frames.tsv files (column 4, microseconds), for frames >= FROM.
// Usage: node tools/frame-stats.mjs FROM FILE...   prints one line per file: mean, p50, p95, max in ms
import fs from 'node:fs';
const [from, ...files] = process.argv.slice(2);
for (const f of files) {
  const t = fs.readFileSync(f, 'utf8').trim().split('\n').map(l => l.split('\t'))
    .filter(c => Number(c[0]) >= Number(from) && c[4] !== 'paused').map(c => Number(c[3]) / 1000);
  const s = [...t].sort((a, b) => a - b);
  const q = p => s[Math.min(s.length - 1, Math.ceil(p * s.length) - 1)];
  const mean = t.reduce((a, b) => a + b, 0) / t.length;
  console.log(`${f}\tframes ${t.length}\tmean ${mean.toFixed(2)}\tp50 ${q(0.5).toFixed(2)}\tp95 ${q(0.95).toFixed(2)}\tmax ${s.at(-1).toFixed(2)}`);
}
