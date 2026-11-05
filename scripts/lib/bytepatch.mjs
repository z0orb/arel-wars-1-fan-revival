// Applies byte patches to entries in a decoded tree. Each must match its expected before-bytes.
// Usage: node bytepatch.mjs <outDir> <originalEntriesDir> spec.json [spec.json ...]
// Spec: {"entry":"lib/armeabi/libgameDSO.so","reason":"...",
//        "edits":[{"offset":"0x1066e0","before":"aabb","after":"ccdd"}]}
// Prints each patched entry name; the output file is <outDir>/<entry>.
import fs from 'node:fs';
import path from 'node:path';

const [outDir, srcDir, ...specs] = process.argv.slice(2);
const buffers = new Map();
for (const f of specs) {
  const spec = JSON.parse(fs.readFileSync(f, 'utf8'));
  if (!buffers.has(spec.entry)) buffers.set(spec.entry, fs.readFileSync(path.join(srcDir, spec.entry)));
  const b = buffers.get(spec.entry);
  for (const e of spec.edits) {
    const off = Number(e.offset);
    const before = Buffer.from(e.before, 'hex');
    const after = Buffer.from(e.after, 'hex');
    if (before.length !== after.length || !before.length) throw new Error(`${f}: bad edit at ${e.offset}`);
    if (!b.subarray(off, off + before.length).equals(before))
      throw new Error(`${f}: before-bytes do not match at ${e.offset} in ${spec.entry}`);
    after.copy(b, off);
  }
}
for (const [entry, b] of buffers) {
  const out = path.join(outDir, entry);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, b);
  console.log(entry);
}
