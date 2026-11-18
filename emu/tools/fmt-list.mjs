// Lists every printf format string of the guest (PLAN.md TASK-E1.2) and checks that the bridge
// formatter (bridge/src/fmt.cpp) handles each conversion.
// Usage: node tools/fmt-list.mjs GUEST.so OUT.txt
// A candidate is a NUL-ended run of printable bytes (4 or more) in a non-executable PROGBITS section
// that holds at least one "%" conversion. This is a superset of the real format strings, so a
// conversion that fmt.cpp does not handle fails the run only if it parses as a conversion.
// The grammar is the grammar of fmt.cpp: flags "-+ #0'", width (digits or *), precision
// (. digits or .*), length (h hh l ll L q j z t), then the conversion character.
import fs from 'node:fs';

const [soPath, outPath] = process.argv.slice(2);
if (!outPath) throw new Error('usage: fmt-list.mjs GUEST.so OUT.txt');
const b = fs.readFileSync(soPath);
const shoff = b.readUInt32LE(0x20), shentsize = b.readUInt16LE(0x2e), shnum = b.readUInt16LE(0x30), shstrndx = b.readUInt16LE(0x32);
const sec = (i) => {
  const o = shoff + i * shentsize;
  return { name: b.readUInt32LE(o), type: b.readUInt32LE(o + 4), flags: b.readUInt32LE(o + 8), addr: b.readUInt32LE(o + 12), offset: b.readUInt32LE(o + 16), size: b.readUInt32LE(o + 20) };
};
const strsec = sec(shstrndx);
const secName = (s) => { let e = strsec.offset + s.name; const st = e; while (b[e]) e++; return b.toString('latin1', st, e); };

const HANDLED = new Set('diuxXocspfFeEgGaA'.split(''));
const conv = /%(%|[-+ #0']*(\*|\d+)?(\.(\*|\d*))?(hh|h|ll|l|L|q|j|z|t)?([a-zA-Z]))/g;
const rows = [], bad = [], words = [], counts = new Map();
for (let i = 0; i < shnum; i++) {
  const s = sec(i);
  if (s.type !== 1 || (s.flags & 0x4) || !(s.flags & 0x2)) continue; // PROGBITS, ALLOC, not EXECINSTR
  const name = secName(s);
  let start = -1;
  for (let p = s.offset; p < s.offset + s.size; p++) {
    const c = b[p];
    const printable = (c >= 0x20 && c < 0x7f) || c === 0x09 || c === 0x0a || c === 0x0d || c >= 0xa1;
    if (printable) { if (start < 0) start = p; continue; }
    if (c === 0 && start >= 0 && p - start >= 2) {
      const str = b.toString('latin1', start, p);
      const convs = [];
      for (const m of str.matchAll(conv)) if (m[1] !== '%') convs.push(m[0]);
      // A table word: a 3-byte "string" at a 4-aligned offset, with a NUL high byte in the word
      // before or after it. So it is one little-endian word below 0x01000000 in a word table.
      const va0 = s.addr + (start - s.offset);
      const tableWord = p - start === 3 && va0 % 4 === 0 && (b[start - 1] === 0 && b[start - 4] !== 0 || b[p + 4] === 0 && b[p + 1] !== 0);
      if (convs.length && tableWord) words.push({ va: va0, name, str });
      else if (convs.length) {
        const va = s.addr + (start - s.offset);
        rows.push({ va, name, str, convs });
        for (const c of convs) {
          const ch = c[c.length - 1];
          counts.set(ch, (counts.get(ch) || 0) + 1);
          if (!HANDLED.has(ch)) bad.push({ va, str, c });
        }
      }
    }
    start = -1;
  }
}
const esc = (s) => s.replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t');
let out = `# Format strings of the guest (tools/fmt-list.mjs). VA, section, conversions, string.\n`;
out += `# ${rows.length} strings. Conversion counts: ${[...counts].sort().map(([k, v]) => `%${k} ${v}`).join(', ')}\n`;
out += `# Not handled by fmt.cpp: ${bad.length ? bad.map((x) => `0x${x.va.toString(16)} ${x.c}`).join(', ') : 'none'}\n`;
out += `# Table words, not strings (${words.length}): ${words.map((w) => `0x${w.va.toString(16)} ${w.name} ${JSON.stringify(w.str)}`).join(', ') || 'none'}\n`;
for (const r of rows) out += `0x${r.va.toString(16)}\t${r.name}\t${r.convs.join(' ')}\t${esc(r.str)}\n`;
fs.writeFileSync(outPath, out);
console.log(`fmt-list: ${rows.length} strings; conversions ${[...counts].sort().map(([k, v]) => `%${k}:${v}`).join(' ')}; not handled: ${bad.length}`);
for (const x of bad.slice(0, 20)) console.log(`  0x${x.va.toString(16)} ${x.c} in ${JSON.stringify(x.str)}`);
process.exit(bad.length ? 1 : 0);
