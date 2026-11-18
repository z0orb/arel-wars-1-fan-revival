// Checks that two APKs hold the same entries with the same bytes, except AndroidManifest.xml and the
// signature files in META-INF (PLAN.md TASK-E8.2: the public APK is the release APK with a new package
// and key). It compares the name, the method, the CRC-32 and both sizes of each entry, and the SHA-256
// of the stored or compressed data. Exit 0 when only the allowed entries differ.
// Usage: node tools/apk-diff.mjs A.apk B.apk
import fs from 'node:fs';
import crypto from 'node:crypto';

const ALLOWED = new Set(['AndroidManifest.xml']);
const isSig = (n) => /^META-INF\/[^/]+$/.test(n) && /(MANIFEST\.MF|\.SF|\.RSA|\.DSA|\.EC)$/i.test(n);

function entries(file) {
  const z = fs.readFileSync(file);
  let e = z.length - 22;
  while (e >= 0 && z.readUInt32LE(e) !== 0x06054b50) e--;
  if (e < 0) throw new Error(`${file}: no end of central directory`);
  const count = z.readUInt16LE(e + 10);
  let p = z.readUInt32LE(e + 16);
  const m = new Map();
  for (let i = 0; i < count; i++) {
    if (z.readUInt32LE(p) !== 0x02014b50) throw new Error(`${file}: bad central directory`);
    const method = z.readUInt16LE(p + 10), crc = z.readUInt32LE(p + 16);
    const csize = z.readUInt32LE(p + 20), usize = z.readUInt32LE(p + 24);
    const nl = z.readUInt16LE(p + 28), xl = z.readUInt16LE(p + 30), cl = z.readUInt16LE(p + 32);
    const off = z.readUInt32LE(p + 42);
    const name = z.subarray(p + 46, p + 46 + nl).toString('utf8');
    const lnl = z.readUInt16LE(off + 26), lxl = z.readUInt16LE(off + 28);
    const data = z.subarray(off + 30 + lnl + lxl, off + 30 + lnl + lxl + csize);
    m.set(name, `${method} ${crc} ${csize} ${usize} ${crypto.createHash('sha256').update(data).digest('hex')}`);
    p += 46 + nl + xl + cl;
  }
  return m;
}

const [a, b] = process.argv.slice(2);
const A = entries(a), B = entries(b);
const bad = [];
let same = 0;
for (const n of new Set([...A.keys(), ...B.keys()])) {
  if (isSig(n)) continue;
  if (A.get(n) === B.get(n)) { same++; continue; }
  if (ALLOWED.has(n) && A.has(n) && B.has(n)) continue;
  bad.push(`${n}: ${A.get(n) ?? 'missing'} / ${B.get(n) ?? 'missing'}`);
}
if (bad.length) { console.log(`APK DIFF FAIL: ${bad.length} entries differ\n${bad.slice(0, 20).join('\n')}`); process.exit(1); }
console.log(`APK DIFF OK: ${same} entries equal, only ${[...ALLOWED].join(', ')} and the signature differ`);
