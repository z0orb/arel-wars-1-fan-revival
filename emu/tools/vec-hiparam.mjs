// A derived vector suite (PLAN.md TASK-E4.4): the cases of IN whose function takes a u16 last
// parameter (mangled name ends in "t"), with high bits set in that parameter. The original leaves
// read it with ldrh (only the low 16 bits). No 8.4 suite passes a value above 0xffff there.
// Usage: node tools/vec-hiparam.mjs IN.vec OUT.vec
// Case format: universal/oracle/src/vec.c (version 1).
import fs from 'node:fs';
const [inp, out] = process.argv.slice(2);
const b = fs.readFileSync(inp);
let o = 0;
const u32 = () => { const v = b.readUInt32LE(o); o += 4; return v; };
if (b.toString('latin1', 0, 4) !== 'AVEC') throw new Error('bad magic');
o = 4;
if (u32() !== 1) throw new Error('bad version');
const nblob = u32();
for (let i = 0; i < nblob; i++) { const len = u32(); o += (len + 3) & ~3; }
const blobEnd = o;
const ncase = u32();
const keep = [];
let k = 0;
for (let c = 0; c < ncase; c++) {
  const start = o;
  const nl = b[o]; const name = b.toString('latin1', o + 1, o + 1 + nl); o += 1 + nl;
  o += 1 + 2 + 2 + 4; // target, sw, sh, seed
  const hasclip = b[o]; o += 1 + (hasclip ? 8 : 0);
  const na = b[o]; o += 1;
  const argStart = o;
  o += 5 * na;
  if (!name.endsWith('t')) continue;
  const rec = Buffer.from(b.subarray(start, o));
  // The last entry is the u16 parameter (tag 0).
  const last = argStart - start + 5 * (na - 1);
  if (rec[last] !== 0) throw new Error(`case ${c}: the last argument of ${name} is not a value`);
  const hi = [0x00010000, 0x7fff0000, 0x80000000, 0xffff0000, 0x12340000][k++ % 5];
  rec.writeUInt32LE((rec.readUInt32LE(last + 1) | hi) >>> 0, last + 1);
  keep.push(rec);
}
const n = Buffer.alloc(4); n.writeUInt32LE(keep.length);
fs.writeFileSync(out, Buffer.concat([b.subarray(0, blobEnd), n, ...keep]));
console.log(`${out}: ${keep.length} cases with a high u16 parameter`);
