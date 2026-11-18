// Screen checks for the emu device gates (PLAN.md GC-E1.3). The PNG decoder is a copy of
// ../../scripts/gates/helpers.mjs (v1). Subcommands print a result and exit non-zero on a failed check.
//   title-match SHOT.png X0 Y0 X1 Y1 MAX REF.rgb565 [REF.rgb565 ...]
//       Box-averages the game box of the screenshot down to the size of the reference frames
//       (runner -png output: u16 w, u16 h, then RGB565). A pixel differs when a channel differs by
//       more than 48 of 255. Prints the smallest fraction over the references; exit 0 when <= MAX.
//   pixels-changed A.png B.png          prints the fraction of pixels that differ
//   not-black A.png                     exit 0 when more than 1% of pixels are visibly lit
//   lit-box A.png                       prints "W H X0 Y0 X1 Y1": the box of all lit pixels (E5)
//   region-changed GOLDEN SHOT X0 Y0 X1 Y1 [BX BY BW BH]   v1 golden against a shot in another box (E5).
//       From E6 it compares at game resolution (the centre of each game pixel). AREL_CMP=screen: each screen pixel
import fs from 'node:fs';
import zlib from 'node:zlib';

const [cmd, ...a] = process.argv.slice(2);
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

function rgb565(file) {
  const b = fs.readFileSync(file);
  const w = b.readUInt16LE(0), h = b.readUInt16LE(2);
  const px = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) {
    const v = b.readUInt16LE(4 + 2 * i);
    px[3 * i] = ((v >> 11) & 31) * 255 / 31;
    px[3 * i + 1] = ((v >> 5) & 63) * 255 / 63;
    px[3 * i + 2] = (v & 31) * 255 / 31;
  }
  return { w, h, px };
}

function downscale(p, x0, y0, x1, y1, w, h) {
  const out = new Float64Array(w * h * 3);
  const sx = (x1 - x0) / w, sy = (y1 - y0) / h;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const ax = Math.floor(x0 + x * sx), bx = Math.max(ax + 1, Math.floor(x0 + (x + 1) * sx));
    const ay = Math.floor(y0 + y * sy), by = Math.max(ay + 1, Math.floor(y0 + (y + 1) * sy));
    let r = 0, g = 0, bl = 0, n = 0;
    for (let yy = ay; yy < by; yy++) for (let xx = ax; xx < bx; xx++) {
      const i = (yy * p.w + xx) * p.bpp;
      r += p.px[i]; g += p.px[i + 1]; bl += p.px[i + 2]; n++;
    }
    out[3 * (y * w + x)] = r / n; out[3 * (y * w + x) + 1] = g / n; out[3 * (y * w + x) + 2] = bl / n;
  }
  return out;
}

function crc32(buf) {
  let c = ~0;
  for (const b of buf) { c ^= b; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xedb88320 : c >>> 1; }
  return ~c >>> 0;
}
function writePng(file, w, h, bpp, px) {
  const raw = Buffer.alloc((w * bpp + 1) * h);
  for (let y = 0; y < h; y++) px.copy(raw, y * (w * bpp + 1) + 1, y * w * bpp, (y + 1) * w * bpp);
  const chunk = (t, d) => {
    const l = Buffer.alloc(4); l.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t, 'latin1'), d]);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td));
    return Buffer.concat([l, td, c]);
  };
  const ih = Buffer.alloc(13);
  ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = bpp === 4 ? 6 : 2;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih),
    chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

switch (cmd) {
  case 'to-landscape': {
    // E6: IN.png OUT.png. Old images (API 21 AVD) give screencap in the natural portrait orientation of
    // the display, with the landscape window turned (display rotation 90). Turn it back:
    // out(x, y) = in(W - 1 - y, x), where W is the width of the portrait input. A landscape input is copied.
    const p = png(a[0]);
    if (p.w >= p.h) { fs.copyFileSync(a[0], a[1]); break; }
    const ow = p.h, oh = p.w, out = Buffer.alloc(ow * oh * p.bpp);
    for (let y = 0; y < oh; y++) for (let x = 0; x < ow; x++) {
      const s = (x * p.w + (p.w - 1 - y)) * p.bpp, d = (y * ow + x) * p.bpp;
      p.px.copy(out, d, s, s + p.bpp);
    }
    writePng(a[1], ow, oh, p.bpp, out);
    break;
  }
  case 'title-match': {
    const shot = png(a[0]);
    const [x0, y0, x1, y1, max] = a.slice(1, 6).map(Number);
    let best = 1;
    for (const f of a.slice(6)) {
      const r = rgb565(f);
      const d = downscale(shot, x0, y0, x1, y1, r.w, r.h);
      let diff = 0;
      for (let i = 0; i < r.w * r.h; i++)
        for (let c = 0; c < 3; c++) if (Math.abs(d[3 * i + c] - r.px[3 * i + c]) > 48) { diff++; break; }
      best = Math.min(best, diff / (r.w * r.h));
    }
    console.log(best.toFixed(4));
    process.exit(best <= max ? 0 : 1);
  }
  case 'pixels-changed': {
    const p = png(a[0]), q = png(a[1]);
    if (p.w !== q.w || p.h !== q.h) { console.log(1); break; }
    let changed = 0;
    for (let i = 0; i < p.px.length; i += p.bpp)
      if (p.px[i] !== q.px[i] || p.px[i + 1] !== q.px[i + 1] || p.px[i + 2] !== q.px[i + 2]) changed++;
    console.log((changed / (p.w * p.h)).toFixed(4));
    break;
  }
  case 'not-black': {
    const { w, h, bpp, px } = png(a[0]);
    let lit = 0;
    for (let i = 0; i < px.length; i += bpp) if (px[i] + px[i + 1] + px[i + 2] > 24) lit++;
    console.log(`lit=${(lit / (w * h)).toFixed(4)}`);
    process.exit(lit / (w * h) > 0.01 ? 0 : 1);
  }
  case 'lit-box': {
    // E5: the smallest box that holds every pixel with r+g+b > 24. Prints "W H X0 Y0 X1 Y1" (X1, Y1 exclusive).
    const { w, h, bpp, px } = png(a[0]);
    let x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * bpp;
      if (px[i] + px[i + 1] + px[i + 2] > 24) {
        if (x < x0) x0 = x; if (x >= x1) x1 = x + 1; if (y < y0) y0 = y; if (y >= y1) y1 = y + 1;
      }
    }
    console.log(`${w} ${h} ${x0} ${y0} ${x1} ${y1}`);
    break;
  }
  case 'region-changed': {
    // E5: GOLDEN.png SHOT.png X0 Y0 X1 Y1 [BX BY BW BH]. The box X0 Y0 X1 Y1 is in v1 screen pixels.
    // The golden is a v1 screenshot (game box x 372, y 0, 1620 x 1080). With BX BY BW BH, each pixel of
    // the shot comes from the same game pixel in that box (nearest). Prints the fraction that differs.
    const g = png(a[0]), s = png(a[1]);
    const [x0, y0, x1, y1] = a.slice(2, 6).map(Number);
    const [bx, by, bw, bh] = a.length >= 10 ? a.slice(6, 10).map(Number) : [372, 0, 1620, 1080];
    // E6: AREL_TOL (default 0) is the largest channel difference that counts as equal. An AVD rounds
    // the colours of the RGB565 frame differently by up to 8 (smoke sets 8 on an AVD only).
    // Optional GBX GBY GBW GBH: the game box of the golden (an emu golden). Default: the v1 box.
    const [gx0, gy0, gw, gh] = a.length >= 14 ? a.slice(10, 14).map(Number) : [372, 0, 1620, 1080];
    // E6: AREL_TOL_SCALED (default 0) replaces AREL_TOL when the two boxes have different sizes. The
    // AVD GL path filters a scaled frame, so a game pixel next to an edge mixes with its neighbour
    // (tablet43 box 2048 x 1365: up to 39 for each channel). smoke sets 32 on an AVD only.
    const scaled = bw !== gw || bh !== gh;
    const T = Math.max(Number(process.env.AREL_TOL || 0), scaled ? Number(process.env.AREL_TOL_SCALED || 0) : 0);
    let changed = 0, total = 0;
    if (process.env.AREL_CMP !== 'screen') {
      // E6 (default): compare at game resolution (480 x 320). The region becomes the game pixels that it
      // touches. Each game pixel is read at its centre in each box, so a box of any scale (tablet 1.26)
      // reads the same game pixel as the v1 box. AREL_CMP=screen gives the E5 compare (each screen pixel).
      const s3 = 1620 / 480;
      const qx0 = Math.max(0, Math.floor((x0 - 372) / s3)), qx1 = Math.min(480, Math.ceil((x1 - 372) / s3));
      const qy0 = Math.max(0, Math.floor(y0 / s3)), qy1 = Math.min(320, Math.ceil(y1 / s3));
      for (let qy = qy0; qy < qy1; qy++) for (let qx = qx0; qx < qx1; qx++) {
        const sx = bx + Math.floor((qx + 0.5) * bw / 480), sy = by + Math.floor((qy + 0.5) * bh / 320);
        const gx = gx0 + Math.floor((qx + 0.5) * gw / 480), gy = gy0 + Math.floor((qy + 0.5) * gh / 320);
        total++;
        if (sx < 0 || sy < 0 || sx >= s.w || sy >= s.h || gx < 0 || gy < 0 || gx >= g.w || gy >= g.h) { changed++; continue; }
        const i = (gy * g.w + gx) * g.bpp, j = (sy * s.w + sx) * s.bpp;
        if (Math.abs(g.px[i] - s.px[j]) > T || Math.abs(g.px[i + 1] - s.px[j + 1]) > T || Math.abs(g.px[i + 2] - s.px[j + 2]) > T) changed++;
      }
      console.log((changed / total).toFixed(4));
      break;
    }
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
      const sx = bx + Math.floor((x - 372) * bw / 1620), sy = by + Math.floor(y * bh / 1080);
      const gx = gx0 + Math.floor((x - 372) * gw / 1620), gy = gy0 + Math.floor(y * gh / 1080);
      total++;
      if (sx < 0 || sy < 0 || sx >= s.w || sy >= s.h || gx < 0 || gy < 0 || gx >= g.w || gy >= g.h) { changed++; continue; }
      const i = (gy * g.w + gx) * g.bpp, j = (sy * s.w + sx) * s.bpp;
      { if (Math.abs(g.px[i] - s.px[j]) > T || Math.abs(g.px[i + 1] - s.px[j + 1]) > T || Math.abs(g.px[i + 2] - s.px[j + 2]) > T) changed++; }
    }
    console.log((changed / total).toFixed(4));
    break;
  }
  default: console.error(`unknown command ${cmd}`); process.exit(2);
}
