// Generates patches/binary/0002-textrel.json from the original .so.
// Removes DT_TEXTREL: every relocation landing in the read-only LOAD segment becomes R_ARM_NONE, and
// later .dynamic entries shift down over the tag. Audit Q7 proves the only users, the libgcc unwinder,
// cannot run. Refuses to write anything if the file differs from what the audit described.
// Usage: node gen-textrel.mjs <original libgameDSO.so> <out.json>
import fs from 'node:fs';

const [input, output] = process.argv.slice(2);
const b = fs.readFileSync(input);
const phoff = b.readUInt32LE(28), phsize = b.readUInt16LE(42), phnum = b.readUInt16LE(44);
let rx, dyn;
for (let i = 0; i < phnum; i++) {
  const p = phoff + i * phsize, type = b.readUInt32LE(p);
  if (type === 1 && !rx) {
    if (b.readUInt32LE(p + 4) !== 0 || b.readUInt32LE(p + 8) !== 0) throw new Error('first LOAD is not identity mapped');
    if (b.readUInt32LE(p + 24) & 2) throw new Error('first LOAD is writable');
    rx = b.readUInt32LE(p + 16);
  }
  if (type === 2) dyn = { off: b.readUInt32LE(p + 4), size: b.readUInt32LE(p + 16) };
}
const tags = new Map();
let textrelAt = -1, end = -1;
for (let q = dyn.off; q < dyn.off + dyn.size; q += 8) {
  const t = b.readUInt32LE(q);
  tags.set(t, b.readUInt32LE(q + 4));
  if (t === 22) textrelAt = q;
  if (t === 0) { end = q; break; }
}
if (textrelAt < 0 || end < 0) throw new Error('DT_TEXTREL or DT_NULL missing');
if (tags.has(30)) throw new Error('unexpected DT_FLAGS');

const edits = [];
const scan = (start, size) => {
  for (let p = start; p < start + size; p += 8) {
    if (b.readUInt32LE(p) >= rx)  continue;
    edits.push({ offset: `0x${(p + 4).toString(16)}`, before: b.subarray(p + 4, p + 8).toString('hex'), after: '00000000' });
  }
};
scan(tags.get(17), tags.get(18)); // .rel.dyn
scan(tags.get(23), tags.get(2));  // .rel.plt
if (edits.length !== 12) throw new Error(`expected 12 read-only relocations, found ${edits.length}`);

const before = b.subarray(textrelAt, end + 8);
const after = Buffer.alloc(before.length);
before.copy(after, 0, 8);
edits.push({ offset: `0x${textrelAt.toString(16)}`, before: before.toString('hex'), after: after.toString('hex') });

fs.writeFileSync(output, JSON.stringify({
  entry: 'lib/armeabi/libgameDSO.so',
  reason: 'Remove DT_TEXTREL for target 23 and above. The 12 read-only relocations become R_ARM_NONE and the tag is removed from .dynamic. Only the unreachable libgcc unwinder uses those literals (audit Q7).',
  edits,
}, null, 2) + '\n');
console.log(`${edits.length} edits written to ${output}`);
