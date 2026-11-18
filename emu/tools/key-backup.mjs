// Emu copy of v1 scripts/lib/key-backup.mjs (TASK-E8.2): encrypted backup of the release key and its password. Run from emu/.
// AES-256-GCM with a scrypt-derived key. Needs only Node to restore.
//   node key-backup.mjs create  <out.enc> <passphrase-file>
//   node key-backup.mjs restore <in.enc> <passphrase-file> <empty-out-dir>
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const FILES = ['keys/release.jks', 'keys/release.pass'];
const MAGIC = Buffer.from('AWKB1');
const SCRYPT = { N: 2 ** 17, r: 8, p: 1, maxmem: 256 * 1024 * 1024 };
const derive = (phrase, salt) => crypto.scryptSync(phrase, salt, 32, SCRYPT);
const readPhrase = (f) => fs.readFileSync(f, 'utf8').trim();

const [cmd, file, passFile, outDir] = process.argv.slice(2);
if (cmd === 'create') {
  const plain = Buffer.from(JSON.stringify(Object.fromEntries(FILES.map((f) => [f, fs.readFileSync(f).toString('base64')]))));
  const salt = crypto.randomBytes(16);
  const iv = crypto.randomBytes(12);
  const c = crypto.createCipheriv('aes-256-gcm', derive(readPhrase(passFile), salt), iv);
  const body = Buffer.concat([c.update(plain), c.final()]);
  fs.writeFileSync(file, Buffer.concat([MAGIC, salt, iv, c.getAuthTag(), body]));
  console.log(`${file}: ${FILES.length} files`);
} else if (cmd === 'restore') {
  const b = fs.readFileSync(file);
  if (!b.subarray(0, 5).equals(MAGIC)) throw new Error('not a key backup');
  if (fs.existsSync(outDir) && fs.readdirSync(outDir).length) throw new Error('output folder must be empty');
  const d = crypto.createDecipheriv('aes-256-gcm', derive(readPhrase(passFile), b.subarray(5, 21)), b.subarray(21, 33));
  d.setAuthTag(b.subarray(33, 49));
  const data = JSON.parse(Buffer.concat([d.update(b.subarray(49)), d.final()]).toString());
  for (const [name, content] of Object.entries(data)) {
    const out = path.join(outDir, name);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, Buffer.from(content, 'base64'));
    console.log(out);
  }
} else {
  console.error('usage: key-backup.mjs create|restore ...');
  process.exit(2);
}
