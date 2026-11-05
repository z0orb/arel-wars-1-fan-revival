// Gate helpers. Subcommands print a result; they exit non-zero on a failed check.
import fs from 'node:fs';
import zlib from 'node:zlib';

const [cmd, ...a] = process.argv.slice(2);
const params = () => JSON.parse(fs.readFileSync('scripts/gates/params.json', 'utf8'));
const lookup = (key) => {
  let v = params();
  for (const k of key.match(/persist\.[\w.]+|[^.]+/g)) v = v?.[k];
  return v;
};

function zipEntries(file) {
  const z = fs.readFileSync(file);
  let e = z.length - 22;
  while (e >= 0 && z.readUInt32LE(e) !== 0x06054b50) e--;
  if (e < 0) throw new Error(`${file}: not a zip`);
  const total = z.readUInt16LE(e + 10);
  let p = z.readUInt32LE(e + 16);
  const out = new Map();
  for (let i = 0; i < total; i++) {
    const n = z.readUInt16LE(p + 28), m = z.readUInt16LE(p + 30), k = z.readUInt16LE(p + 32);
    out.set(z.toString('utf8', p + 46, p + 46 + n), { method: z.readUInt16LE(p + 10), crc: z.readUInt32LE(p + 16) });
    p += 46 + n + m + k;
  }
  return out;
}

function png(file) {
  const b = fs.readFileSync(file);
  let p = 8, w, h, ct;
  const idat = [];
  while (p < b.length) {
    const len = b.readUInt32BE(p), type = b.toString('latin1', p + 4, p + 8);
    if (type === 'IHDR') {
      w = b.readUInt32BE(p + 8);
      h = b.readUInt32BE(p + 12);
      ct = b[p + 17];
      if (b[p + 16] !== 8) throw new Error('need 8-bit PNG');
    }
    if (type === 'IDAT') idat.push(b.subarray(p + 8, p + 8 + len));
    p += 12 + len;
  }
  const bpp = { 2: 3, 6: 4 }[ct];
  if (!bpp) throw new Error(`unsupported PNG colour type ${ct}`);
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = w * bpp, px = Buffer.alloc(stride * h);
  for (let y = 0, q = 0; y < h; y++) {
    const f = raw[q++];
    for (let x = 0; x < stride; x++, q++) {
      const l = x >= bpp ? px[y * stride + x - bpp] : 0;
      const u = y ? px[(y - 1) * stride + x] : 0;
      const c = y && x >= bpp ? px[(y - 1) * stride + x - bpp] : 0;
      let v = raw[q];
      if (f === 1) v += l;
      else if (f === 2) v += u;
      else if (f === 3) v += (l + u) >> 1;
      else if (f === 4) {
        const t = l + u - c, pa = Math.abs(t - l), pb = Math.abs(t - u), pc = Math.abs(t - c);
        v += pa <= pb && pa <= pc ? l : pb <= pc ? u : c;
      }
      px[y * stride + x] = v & 255;
    }
  }
  return { w, h, bpp, px };
}

switch (cmd) {
  case 'param': {
    const v = lookup(a[0]);
    if (v === null || v === undefined) { console.error(`param ${a[0]} is not set`); process.exit(1); }
    console.log(typeof v === 'object' ? JSON.stringify(v) : String(v));
    break;
  }
  case 'box': { // prints a four-number box param as separate words
    const v = lookup(a[0]);
    if (!Array.isArray(v) || v.length !== 4) { console.error(`box ${a[0]} is not set`); process.exit(1); }
    console.log(v.join(' '));
    break;
  }
  case 'steps': { // prints the steps of a macro; fails if it is not set
    const v = lookup(a[0]);
    if (!Array.isArray(v) || !v.length) { console.error(`macro ${a[0]} is not set`); process.exit(1); }
    for (const s of v) console.log(s);
    break;
  }
  case 'count': { // number of items in a list param
    const v = lookup(a[0]);
    if (!Array.isArray(v) || !v.length) { console.error(`${a[0]} is not set`); process.exit(1); }
    console.log(v.length);
    break;
  }
  case 'pillarbox': { // png: both pillars black, game area drawn, screen landscape
    const { w, h, bpp, px } = png(a[0]);
    const p = params();
    const lit = (x0, x1) => {
      let n = 0, t = 0;
      for (let y = 0; y < h; y += 4) for (let x = x0; x < x1; x += 4) {
        const o = y * w * bpp + x * bpp;
        t++;
        if (px[o] > 8 || px[o + 1] > 8 || px[o + 2] > 8) n++;
      }
      return n / t;
    };
    const pillars = p.pillar_x.map(([x0, x1]) => lit(x0, x1));
    const game = lit(p.game_x[0], p.game_x[1]);
    console.log(`size=${w}x${h} pillars=${pillars.map((v) => v.toFixed(4)).join(',')} game=${game.toFixed(4)}`);
    process.exit(w > h && pillars.every((v) => v === 0) && game > 0.5 ? 0 : 1);
  }
  case 'zip-compare': { // original new
    const o = zipEntries(a[0]), n = zipEntries(a[1]);
    const meta = (s) => s.startsWith('META-INF/');
    const allowed = new Set(['classes.dex', 'AndroidManifest.xml']);
    const problems = [], differ = [];
    for (const [k, v] of o) {
      if (meta(k)) continue;
      const w = n.get(k);
      if (!w) { problems.push(`missing ${k}`); continue; }
      if (w.method !== v.method) problems.push(`storage method changed ${k}: ${v.method} -> ${w.method}`);
      if (w.crc !== v.crc) { differ.push(k); if (!allowed.has(k)) problems.push(`content changed ${k}`); }
    }
    for (const k of n.keys()) if (!meta(k) && !o.has(k)) problems.push(`added ${k}`);
    console.log(`entries=${o.size} differ=[${differ.join(',')}]`);
    problems.forEach((p) => console.log(p));
    process.exit(problems.length ? 1 : 0);
  }
  case 'ogg-methods': { // original new: every ogg keeps its storage method
    const o = zipEntries(a[0]), n = zipEntries(a[1]);
    let count = 0, bad = 0;
    for (const [k, v] of o) {
      if (!k.endsWith('.ogg')) continue;
      count++;
      if (n.get(k)?.method !== v.method) { bad++; console.log(`changed ${k}`); }
    }
    console.log(`oggs=${count} changed=${bad}`);
    process.exit(count === 68 && !bad ? 0 : 1);
  }
  case 'elf-textrel': { // extracted .so: exit 0 when DT_TEXTREL is absent
    const b = fs.readFileSync(a[0]);
    const phoff = b.readUInt32LE(28), phsize = b.readUInt16LE(42), phnum = b.readUInt16LE(44);
    let found = false, flags = 0;
    for (let i = 0; i < phnum; i++) {
      const p = phoff + i * phsize;
      if (b.readUInt32LE(p) !== 2) continue;
      const off = b.readUInt32LE(p + 4), size = b.readUInt32LE(p + 16);
      for (let q = off; q < off + size; q += 8) {
        const tag = b.readUInt32LE(q), val = b.readUInt32LE(q + 4);
        if (tag === 0) break;
        if (tag === 22) found = true;
        if (tag === 30) flags = val;
      }
    }
    if (flags & 4) found = true; // DF_TEXTREL
    // The tag alone proves little: a relocation left in the read-only segment would crash at load.
    let rx = 0, ro = 0, dyn = {};
    for (let i = 0; i < phnum; i++) {
      const p = phoff + i * phsize;
      if (b.readUInt32LE(p) === 1 && !(b.readUInt32LE(p + 24) & 2) && !rx) rx = b.readUInt32LE(p + 8) + b.readUInt32LE(p + 20);
      if (b.readUInt32LE(p) === 2) for (let q = b.readUInt32LE(p + 4); ; q += 8) { const t = b.readUInt32LE(q); if (!t) break; dyn[t] = b.readUInt32LE(q + 4); }
    }
    for (const [s, n] of [[dyn[17], dyn[18]], [dyn[23], dyn[2]]])
      for (let p = s; p < s + n; p += 8) if (b.readUInt32LE(p) < rx && (b.readUInt32LE(p + 4) & 255)) ro++;
    console.log(`textrel=${found} readonly-relocs=${ro}`);
    process.exit(found || ro ? 1 : 0);
  }
  case 'not-black': { // exit 0 when more than 1% of pixels are visibly lit
    const { w, h, bpp, px } = png(a[0]);
    let lit = 0;
    for (let i = 0; i < px.length; i += bpp) if (px[i] + px[i + 1] + px[i + 2] > 24) lit++;
    const frac = lit / (w * h);
    console.log(`lit=${frac.toFixed(4)}`);
    process.exit(frac > 0.01 ? 0 : 1);
  }
  case 'pixels-differ': { // a b: exit 0 when at least 0.25% of pixels changed
    // A static app showed up to 0.18% change from the status bar and system redraws (tested 2026-11-05).
    const p = png(a[0]), q = png(a[1]);
    if (p.w !== q.w || p.h !== q.h) process.exit(0);
    let changed = 0;
    for (let i = 0; i < p.px.length; i += p.bpp) {
      if (p.px[i] !== q.px[i] || p.px[i + 1] !== q.px[i + 1] || p.px[i + 2] !== q.px[i + 2]) changed++;
    }
    process.exit(changed / (p.w * p.h) >= 0.0025 ? 0 : 1);
  }
  case 'region-changed': { // a b x0 y0 x1 y1: prints the fraction of pixels that differ inside the box
    const p = png(a[0]), q = png(a[1]);
    if (p.w !== q.w || p.h !== q.h) { console.log(1); break; }
    const [x0, y0, x1, y1] = a.slice(2).map(Number);
    let changed = 0, total = 0;
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
      const i = (y * p.w + x) * p.bpp;
      total++;
      if (p.px[i] !== q.px[i] || p.px[i + 1] !== q.px[i + 1] || p.px[i + 2] !== q.px[i + 2]) changed++;
    }
    console.log((changed / total).toFixed(4));
    break;
  }
  case 'pixels-changed': { // a b: prints the fraction of pixels that differ
    const p = png(a[0]), q = png(a[1]);
    if (p.w !== q.w || p.h !== q.h) { console.log(1); break; }
    let changed = 0;
    for (let i = 0; i < p.px.length; i += p.bpp) {
      if (p.px[i] !== q.px[i] || p.px[i + 1] !== q.px[i + 1] || p.px[i + 2] !== q.px[i + 2]) changed++;
    }
    console.log((changed / (p.w * p.h)).toFixed(4));
    break;
  }
  case 'at-most': process.exit(Number(a[0]) <= Number(a[1]) ? 0 : 1);
  case 'at-least': process.exit(Number(a[0]) >= Number(a[1]) ? 0 : 1);
  case 'netstats': { // file uid: total bytes for that uid
    let total = 0, on = false;
    for (const line of fs.readFileSync(a[0], 'utf8').split(/\r?\n/)) {
      const m = line.match(/\buid=(-?\d+)\b/);
      if (m) on = m[1] === a[1];
      else if (on) for (const r of line.matchAll(/\b[rt]b=(\d+)/g)) total += Number(r[1]);
    }
    console.log(total);
    break;
  }
  default:
    console.error(`unknown helper ${cmd}`);
    process.exit(2);
}
