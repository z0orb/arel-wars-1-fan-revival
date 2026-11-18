// Puts several 480x320 RGB565 frame dumps into one PNG grid, at half size, with the frame number
// drawn as a white bar length in the corner is not needed: the order is left to right, top to bottom.
// Usage: node tools/montage.mjs OUT.png COLS FILE.rgb565...   (env FULL=1: full size, for exact tap coordinates)
import fs from 'node:fs';
import zlib from 'node:zlib';
const [out, colsArg, ...files] = process.argv.slice(2);
const cols = Number(colsArg), rows = Math.ceil(files.length / cols);
const S = process.env.FULL === '1' ? 1 : 2, W = 480 / S, H = 320 / S, GW = cols * W + (cols - 1) * 2, GH = rows * H + (rows - 1) * 2;
const img = Buffer.alloc(GW * GH * 3, 0x40);
files.forEach((f, k) => {
  const b = fs.readFileSync(f), w = b.readUInt16LE(0), h = b.readUInt16LE(2);
  const ox = (k % cols) * (W + 2), oy = Math.floor(k / cols) * (H + 2);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const sx = Math.min(w - 1, x * S), sy = Math.min(h - 1, y * S);
    const v = b.readUInt16LE(4 + 2 * (sy * w + sx));
    const r = (v >> 11) & 31, g = (v >> 5) & 63, bl = v & 31, o = 3 * ((oy + y) * GW + ox + x);
    img[o] = (r << 3) | (r >> 2); img[o + 1] = (g << 2) | (g >> 4); img[o + 2] = (bl << 3) | (bl >> 2);
  }
});
const crcT = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = (b) => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
const raw = Buffer.alloc((GW * 3 + 1) * GH);
for (let y = 0; y < GH; y++) img.copy(raw, y * (GW * 3 + 1) + 1, y * GW * 3, (y + 1) * GW * 3);
const ih = Buffer.alloc(13); ih.writeUInt32BE(GW, 0); ih.writeUInt32BE(GH, 4); ih[8] = 8; ih[9] = 2;
fs.writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
console.log(`wrote ${out} ${GW}x${GH}`);
