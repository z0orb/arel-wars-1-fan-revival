// Writes docs/NATIVE.tsv (PLAN.md TASK-E4.7): one row per native replacement.
// Columns: family, mangled name, Ghidra address, the suites that call it, cases that call it.
// Usage: node tools/native-table.mjs NATIVE-LIST DISASM OUT.tsv SUMMARY...
//   NATIVE-LIST: the output of "arel-emu -native-list" (family TAB name).
//   SUMMARY: summary.txt files of "vec-proof run on" runs, named .../von-SUITE/summary.txt or summary-on-SUITE.txt.
import fs from 'node:fs';
const [listPath, disasmPath, out, ...sums] = process.argv.slice(2);
const addr = new Map();
for (const m of fs.readFileSync(disasmPath, 'utf8').matchAll(/^([0-9a-f]{8}) <([^>]+)>:$/gm)) addr.set(m[2], parseInt(m[1], 16) + 0x10000);
const calls = new Map();
for (const f of sums) {
  const suite = (/von-([a-z0-9]+)[\\/]summary\.txt$/.exec(f) || /summary-on-([a-z0-9]+)\.txt$/.exec(f))[1];
  for (const l of fs.readFileSync(f, 'utf8').split('\n')) {
    const c = l.split('\t');
    if (c[0] !== 'native_calls') continue;
    const e = calls.get(c[1]) ?? {suites: [], n: 0};
    e.suites.push(suite); e.n += +c[2]; calls.set(c[1], e);
  }
}
const rows = ['family\tname\tghidra\tsuites\tcases'];
for (const l of fs.readFileSync(listPath, 'utf8').split('\n').filter(Boolean)) {
  const [fam, name] = l.trim().split('\t');
  const a = addr.get(name);
  if (a === undefined) throw new Error(`no address for ${name}`);
  const e = calls.get(name) ?? {suites: ['-'], n: 0};
  rows.push(`${fam}\t${name}\t0x${a.toString(16)}\t${e.suites.sort().join(',')}\t${e.n}`);
}
fs.writeFileSync(out, rows.join('\n') + '\n');
console.log(`${out}: ${rows.length - 1} replacements, ${[...calls.values()].length} called`);
