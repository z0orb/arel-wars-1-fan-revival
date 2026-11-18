// Checks v1 save files as the game does on load (CGsEncryptFile::LoadBegin, PLAN.md DES-14).
// Usage: node tools/save-check.mjs GUEST.so FILE...
// For each Save%d.dat or Sng%d.dat: the size word at offset 0 equals the file size, the device ID
// (64 bytes at offset 8, after the XOR stream) and the checksum (the last 4 bytes). option.sav is not
// encrypted, so only its size (0x74) is checked. Prints one line per file: "ok NAME ID" or "bad NAME REASON".
// Exit 1 if a file is bad. The cipher: guest RNG (state * 214013 + 2531011) seeded with the header time,
// each byte XOR key[Random(16)], key = 16 bytes at ELF VA 0x108694 of the guest (read from GUEST.so).
import fs from 'node:fs';
import path from 'node:path';

const [so, ...files] = process.argv.slice(2);
if (!so || !files.length) { console.error('usage: save-check.mjs GUEST.so FILE...'); process.exit(2); }
const key = fs.readFileSync(so).subarray(0x108694, 0x108694 + 16);

export function check(buf) {
  if (buf.length < 0x4c) return { bad: 'file too short' };
  if (buf.readUInt32LE(0) !== buf.length) return { bad: 'size word is not the file size' };
  const b = Buffer.from(buf);
  const ts = b.readUInt32LE(4);
  let s = ts;
  const rnd = (n) => { s = (Math.imul(s, 214013) + 2531011) >>> 0; return ((s & 0x7fffffff) >>> 16) % n; };
  for (let i = 8; i < b.length; i++) b[i] ^= key[rnd(16)];
  const raw = b.subarray(8, 72);
  const id = raw.subarray(0, raw.indexOf(0) < 0 ? 64 : raw.indexOf(0)).toString('latin1');
  s = ts;
  let c = rnd(0xffffff) >>> 0;
  for (let i = 0; i < b.length - 4; i++) { const x = b[i]; c = ((c >>> 24) ^ (x ^ key[x & 15]) ^ (c << 8)) >>> 0; }
  if (c !== b.readUInt32LE(b.length - 4)) return { bad: 'checksum error', id };
  return { id };
}

let fails = 0;
for (const f of files) {
  const name = path.basename(f);
  const buf = fs.readFileSync(f);
  let r;
  if (name === 'option.sav') r = buf.length === 0x74 ? { id: '-' } : { bad: 'size is not 0x74' };
  else r = check(buf);
  if (r.bad) { fails++; console.log(`bad ${name} ${r.bad}`); } else console.log(`ok ${name} ${r.id}`);
}
process.exit(fails ? 1 : 0);
