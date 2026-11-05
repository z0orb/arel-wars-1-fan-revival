// Replaces strings in GST string tables stored as .zt1 entries. Each edit must match its expected text.
// .zt1: u32 whole file size, u32 inflated size, then zlib.
// GST: "GST\1", u16 count, u16 offsets, u16 total length, NUL-ended strings.
// Usage: node zt1text.mjs <outDir> <decodedDir> spec.json [...]
// Spec: {"entry":"assets/data_eng/StrString.zt1","encoding":"latin1"|"euc-kr",
//        "edits":[{"index":494,"before":"...","after":"..."}]}
// Prints each patched entry name; the output file is <outDir>/<entry>.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const [outDir, srcDir, ...specs] = process.argv.slice(2);

let eucKr;
function eucKrTable() {
  if (eucKr) return eucKr;
  eucKr = new Map();
  const d = new TextDecoder('euc-kr', { fatal: true });
  for (let a = 0xb0; a <= 0xc8; a++) {
    for (let b = 0xa1; b <= 0xfe; b++) {
      try { eucKr.set(d.decode(Uint8Array.of(a, b)), [a, b]); } catch { /* unassigned */ }
    }
  }
  return eucKr;
}
function encode(text, encoding) {
  if (encoding === 'latin1') return Buffer.from(text, 'latin1');
  const out = [];
  for (const ch of text) {
    if (ch.codePointAt(0) < 0x80) out.push(ch.codePointAt(0));
    else {
      const pair = eucKrTable().get(ch);
      if (!pair) throw new Error(`no EUC-KR code for ${ch}`);
      out.push(...pair);
    }
  }
  return Buffer.from(out);
}
function decode(bytes, encoding) {
  return encoding === 'latin1' ? bytes.toString('latin1') : new TextDecoder('euc-kr').decode(bytes);
}

for (const f of specs) {
  const spec = JSON.parse(fs.readFileSync(f, 'utf8'));
  const raw = fs.readFileSync(path.join(srcDir, spec.entry));
  if (raw.readUInt32LE(0) !== raw.length) throw new Error(`${spec.entry}: bad zt1 header`);
  const gst = zlib.inflateSync(raw.subarray(8));
  if (gst.subarray(0, 4).toString('latin1') !== 'GST\x01') throw new Error(`${spec.entry}: not a GST table`);
  const count = gst.readUInt16LE(4);
  const strings = [];
  for (let i = 0; i < count; i++) {
    const s = gst.readUInt16LE((i + 3) * 2);
    let e = s;
    while (e < gst.length && gst[e]) e++;
    strings.push(gst.subarray(s, e));
  }
  for (const e of spec.edits) {
    if (decode(strings[e.index], spec.encoding) !== e.before) throw new Error(`${f}: string ${e.index} does not match`);
    strings[e.index] = encode(e.after, spec.encoding);
  }
  // After the offsets comes one more word, the table's total length.
  if (gst.readUInt16LE((count + 3) * 2) !== gst.length) throw new Error(`${spec.entry}: no length word`);
  const head = Buffer.alloc((count + 4) * 2);
  gst.copy(head, 0, 0, 6);
  let at = head.length;
  strings.forEach((s, i) => { head.writeUInt16LE(at, (i + 3) * 2); at += s.length + 1; });
  if (at > 0xffff) throw new Error(`${spec.entry}: table too large for u16 offsets`);
  head.writeUInt16LE(at, (count + 3) * 2);
  const body = Buffer.concat([head, ...strings.flatMap((s) => [s, Buffer.alloc(1)])]);
  const z = zlib.deflateSync(body, { level: 9 });
  const hdr = Buffer.alloc(8);
  hdr.writeUInt32LE(z.length + 8, 0);
  hdr.writeUInt32LE(body.length, 4);
  const out = path.join(outDir, spec.entry);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, Buffer.concat([hdr, z]));
  console.log(spec.entry);
}
