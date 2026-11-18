// GC-E0.7: every row of the "## Downloads" table in emu/docs/TOOLS.md has a 64-hex SHA-256.
// Usage: node tools/tools-check.mjs docs/TOOLS.md
import { readFileSync } from 'node:fs';
const lines = readFileSync(process.argv[2], 'utf8').split(/\r?\n/);
const start = lines.findIndex(l => /^## Downloads\b/.test(l));
if (start < 0) { console.log('tools-check FAIL: no "## Downloads" section'); process.exit(1); }
let header = null, rows = 0, bad = 0;
for (let i = start + 1; i < lines.length && !/^## /.test(lines[i]); i++) {
  const l = lines[i];
  if (!l.startsWith('|')) continue;
  const cells = l.split('|').slice(1, -1).map(c => c.trim());
  if (!header) { header = cells; continue; }
  if (cells.every(c => /^-+$/.test(c))) continue;
  rows++;
  const col = header.findIndex(h => /SHA-256/.test(h));
  const ok = col >= 0 && /[0-9a-f]{64}/.test(cells[col] || '');
  if (!ok) { bad++; console.log(`no SHA-256: ${cells[0]}`); }
}
if (rows === 0) { console.log('tools-check FAIL: the Downloads table has no rows'); process.exit(1); }
if (bad) { console.log(`tools-check FAIL: ${bad} of ${rows} rows have no SHA-256`); process.exit(1); }
console.log(`tools-check ok: ${rows} rows`);
