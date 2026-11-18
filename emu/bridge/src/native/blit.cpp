// The DrawOP_*_Compress_16* leaves: a C++ port of universal/engine/src/arel/engine/gfx/Blit.java
// (spec/render.md). Same rules, same order. t[] is guest memory as pixels, s[] as bytes.
// d is a pixel index into t[], p and ap are byte addresses into s[], pal is a pixel index.
#include "native.h"

namespace emu {
namespace nat {

static inline int u16(const uint8_t *s, int o) { return s[o] | s[o + 1] << 8; }

int auto_format(int p) {
    int k = (-u16(BY(), p) - 3) & 0xffff;
    return k < 5 ? EX + k : BASIC;
}

static bool has_alpha_leaves(int op) {
    return op == COPY || op == ADD || op == BLEND16 || op == GRAY || op == LIGHTEN || op == LIGHTING || op == FLIP;
}

void draw_auto(int op, bool clip, int d, int p, int pal, int pitch, int cx, int cw, int cy, int ch, int param) {
    int fmt = auto_format(p);
    if (fmt >= ALPHA && !has_alpha_leaves(op)) fmt = fmt == ALPHA ? BASIC : fmt == EX_ALPHA ? EX : S16;
    draw(op, fmt, clip, d, p, pal, pitch, cx, cw, cy, ch, param);
}

static bool active(int op, int param) {
    switch (op) {
    case ADD: case SUB: return param != 0 && param <= 255;
    case VOID: return param != 0;
    case LIGHTING: return param <= 31;
    case FX: return param >= 0 && param <= 3;
    case BLEND16: return param >= 1 && param <= 15;
    case GRAY: return param >= 0;
    default: return true;
    }
}

static const int HALVES[4] = {0xf7de, 0xe79c, 0xc718, 0x8610};

__attribute__((always_inline)) static inline int mix_i(int c, int w) {
    int r = 0;
    for (int j = 1; j <= 4; j++) if ((w & (16 >> j)) != 0) r += (c & HALVES[j - 1]) >> j;
    return r;
}
int mix(int c, int w) { return mix_i(c, w); }

static bool active_a(int op, int param) { return op == BLEND16 ? param != 0 && param <= 15 : active(op, param); }

static inline int fx(int i) { return BY()[GHIDRA(FX_TABLE) + i]; }

__attribute__((always_inline)) static inline int kernel(int op, int s, int t, int k) {
    switch (op) {
    case COPY: return s;
    case ADD: {
        int r, g, b;
        if (k == 255) {
            r = (s >> 11) + (t >> 11); g = ((s >> 5) & 0x3f) + ((t >> 5) & 0x3f); b = (s & 0x1f) + (t & 0x1f);
        } else {
            r = ((s >> 11) * k >> 8) + (t >> 11);
            g = (((s >> 5) & 0x3f) * k >> 8) + ((t >> 5) & 0x3f);
            b = ((s & 0x1f) * k >> 8) + (t & 0x1f);
        }
        return ushr(imin(r, MUT(1) ? 30 : 31) << 16, 5) | ushr(imin(g, 63) << 16, 11) | imin(b, 31);
    }
    case SUB: {
        if (k == 255 && !MUT(2)) {
            int r = imax((t >> 11) - (s >> 11), 0), g = imax(((t >> 5) & 0x3f) - ((s >> 5) & 0x3f), 0), b = imax((t & 0x1f) - (s & 0x1f), 0);
            return r << 11 | g << 5 | b;
        }
        int vr = ushr((s >> 11) * k << 8, 16), vg = ushr((((s >> 5) & 0x3f) * k) << 8, 16), vb = ushr(((s & 0x1f) * k) << 8, 16);
        int tr = t >> 11, tg = (t >> 5) & 0x3f, tb = t & 0x1f;
        int r = tr > vr ? ushr((tr - vr) << 27, 16) : 0;
        int g = tg > vg ? ushr((tg - vg) << 21, 16) : 0;
        int b = tb > vb ? (tb - vb) & 0xffff : 0;
        return r | g | b;
    }
    case LIGHTEN:
        return imax(s & 0xf800, t & 0xf800) | imax(s & 0x7e0, t & 0x7e0) | imax(s & 0x1f, t & 0x1f);
    case LIGHTING: {
        int r = imin((s >> 11) * k >> 4, 31), b = imin((s & 0x1f) * k >> 4, 31), g = imin(((s & 0x7e0) >> 5) * k >> 4, 63);
        return ushr(r << 16, 5) | b | ushr(g << 16, 11);
    }
    case BLEND16: return mix_i(t, 16 - k) + mix_i(s, k);
    case GRAY: {
        int v = (s & 0x1f) + 3 + (s >> 11) + ((s >> 6) & 0x1f);
        int y = (v >> 4) + (v >> 2) + (v >> 6);
        int g = (y << 11 | y << 6 | y) & 0xffff;
        return k == 0 || k > 15 ? g : mix_i(s, 16 - k) + mix_i(g, k);
    }
    case FX: {
        int o = k << 10;
        return fx(MUT(3) ? o + (s >> 11) * 32 + (t >> 11) : o + (t >> 11) * 32 + (s >> 11)) << 11 | fx(o + (t & 0x1f) * 32 + (s & 0x1f))
             | fx(o + ((t >> 6) & 0x1f) * 32 + ((s >> 6) & 0x1f)) << 6;
    }
    case RGB: return k & 0xffff;
    case RGBHALF: return ((s & 0xf7de) >> 1) + ((k & 0xf7de) >> 1);
    case RGBADD: {
        int c = k & 0xffff;
        return imin((s >> 11) + (c >> 11), 31) << 11 | imin((s & 0x1f) + (c & 0x1f), 31)
             | imin(((s >> 5) & 0x3f) + ((c >> 5) & 0x3f), 63) << 5;
    }
    case RGBMULTI: {
        int c = k & 0xffff;
        return ushr(ushr(s, 11) * (c >> 11), 5) << 11 | ((s & 0x1f) * (c & 0x1f)) >> 5
             | ushr(((s & 0x7e0) >> 5) * ((c & 0x7e0) >> 5), 6) << 5;
    }
    default: die("native: kernel op %d", op);
    }
}

__attribute__((always_inline)) static inline int blend_a(int c, int t, int a) {
    int na = MUT(10) ? 256 - a : 255 - a;
    return ((c >> 11) * a + (t >> 11) * na) >> 8 << 11 | (((c >> 5) & 0x3f) * a + ((t >> 5) & 0x3f) * na) >> 8 << 5
         | ((c & 0x1f) * a + (t & 0x1f) * na) >> 8;
}

__attribute__((always_inline)) static inline int kernel_a(int op, int s, int t, int k, int a) {
    int na = 255 - a;
    switch (op) {
    case COPY: return blend_a(s, t, a);
    case ADD: {
        int q = ushr((a * k) << 16, 24);
        int r = imin(((s >> 11) * q >> 8) + (t >> 11), 31), g = imin((((s >> 5) & 0x3f) * q >> 8) + ((t >> 5) & 0x3f), 63),
            b = imin(((s & 0x1f) * q >> 8) + (t & 0x1f), 31);
        return ushr(r << 16, 5) | ushr(g << 16, 11) | b;
    }
    case BLEND16: return blend_a(s, t, ushr((a * ((k - 1) * 17)) << 16, 24));
    case GRAY: {
        if (k >= 1 && k <= 15) return blend_a(kernel(GRAY, s, t, k), t, a);
        int v = (s & 0x1f) + 3 + (s >> 11) + ((s >> 6) & 0x1f);
        int y = (v >> 4) + (v >> 2) + (v >> 6);
        return ushr((y * a + (t >> 11) * na) << 8, 16) << 11 | ushr((y * a + ((t >> 6) & 0x1f) * na) << 8, 16) << 6
             | (y * a + (t & 0x1f) * na) >> 8;
    }
    case LIGHTEN: {
        int c = blend_a(s, t, a);
        return imax(c & 0xf800, t & 0xf800) | imax(c & 0x7e0, t & 0x7e0) | imax(c & 0x1f, t & 0x1f);
    }
    case LIGHTING: {
        int lr = imin((s >> 11) * k >> 4, 31) & 0xffff, lg = imin(((s & 0x7e0) >> 5) * k >> 4, 63) & 0xffff,
            lb = imin((s & 0x1f) * k >> 4, 31) & 0xffff;
        int r = (lr * a + (t >> 11) * na) >> 8, g = (lg * a + ((t >> 5) & 0x3f) * na) >> 8, b = (lb * a + (t & 0x1f) * na) >> 8;
        return ushr(r << 16, 5) | ushr(g << 16, 11) | b;
    }
    default: die("native: op %d has no alpha leaf", op);
    }
}

// The pixels of one literal or fill run (the inner loop of Blit.draw), specialised per op, alpha and
// pixel size, so the kernel and the format tests leave the loop. A run wholly inside [cx, x_end)
// skips the clip test per pixel: the result is the same.
using RunFn = void (*)(uint16_t *t, const uint8_t *s, int &d, int &p, int &ap, int &x, int n, int dx, bool fill,
                       int pal, int cx, int x_end, int param);

template <int OP, bool ALPHA, bool WORDS>
static void run_px(uint16_t *t, const uint8_t *s, int &d, int &p, int &ap, int &x, int n, int dx, bool fill,
                   int pal, int cx, int x_end, int param) {
    constexpr int ps = WORDS ? 2 : 1;
    // Pixels i in [lo, hi) of the run are inside the clip. The others only move d, p and ap.
    int lo = 0, hi = n;
    if (!(x >= cx && x + n <= x_end)) { lo = imin(imax(cx - x, 0), n); hi = imax(imin(x_end - x + (MUT(20) ? 1 : 0), n), lo); }
    // Host pointers in locals: the stores to t[] must not reload d, p and ap through the references.
    const int pstep = fill ? 0 : ps;
    uint16_t *dp = t + (ptrdiff_t)(d + lo * dx);
    const uint8_t *sp = s + (ptrdiff_t)(uint32_t)(p + lo * pstep);
    const uint8_t *app = s + (ptrdiff_t)(uint32_t)(ap + (ALPHA ? lo : 0));
    const uint16_t *pl = t + (ptrdiff_t)pal;
    for (int i = lo; i < hi; i++, dp += dx, sp += pstep) {
        int c = WORDS ? (sp[0] | sp[1] << 8) : pl[*sp];
        *dp = (uint16_t)(ALPHA ? kernel_a(OP, c, *dp, param, *app) : kernel(OP, c, *dp, param));
        if (ALPHA) app++;
    }
    d += n * dx;
    if (ALPHA) ap += n;
    p += n * pstep;
    x += n;
}

template <int OP>
static RunFn pick3(bool alpha, bool words) {
    if (alpha) return words ? run_px<OP, true, true> : run_px<OP, true, false>;
    return words ? run_px<OP, false, true> : run_px<OP, false, false>;
}

static RunFn pick_run(int op, bool alpha, bool words) {
    switch (op) {
    case COPY: return pick3<COPY>(alpha, words);
    case BLEND16: return pick3<BLEND16>(alpha, words);
    case ADD: return pick3<ADD>(alpha, words);
    case SUB: return pick3<SUB>(alpha, words);
    case LIGHTEN: return pick3<LIGHTEN>(alpha, words);
    case GRAY: return pick3<GRAY>(alpha, words);
    case RGB: return pick3<RGB>(alpha, words);
    case RGBHALF: return pick3<RGBHALF>(alpha, words);
    case RGBADD: return pick3<RGBADD>(alpha, words);
    case RGBMULTI: return pick3<RGBMULTI>(alpha, words);
    case LIGHTING: return pick3<LIGHTING>(alpha, words);
    case FX: return pick3<FX>(alpha, words);
    default: die("native: draw op %d", op);
    }
}

void draw(int op, int fmt, bool clip, int d, int p, int pal, int pitch, int cx, int cw, int cy, int ch, int param) {
    uint16_t *t = PX();
    const uint8_t *s = BY();
    if (op == LIGHTING && param == 16) op = COPY;
    int dx = 1, row_step = pitch;
    if (op == FLIP) {
        if (param < 0 || param > 2) return;
        dx = param == 1 ? 1 : -1;
        row_step = param == 0 ? pitch : -pitch;
        op = COPY;
        param = 0;
    }
    bool alpha = fmt >= ALPHA;
    if (alpha ? !active_a(op, param) : !active(op, param)) return;
    int h = u16(s, p), ap = 0;
    if (alpha) {
        if (h != (fmt == ALPHA ? 0xfffb : fmt == EX_ALPHA ? 0xfffa : 0xfff9)) return;
        ap = p + (fmt == S16_ALPHA ? 0 : 2) + (u16(s, p + 2) | u16(s, p + 4) << 16);
        p += 10;
        fmt = fmt == ALPHA ? BASIC : fmt == EX_ALPHA ? EX : S16;
    } else if (fmt == EX) {
        if (h != 0xfffd && (h != 0xfffa || (clip ? op != COPY : op == VOID))) return;
        p += h == 0xfffa ? 10 : 2;
    } else if (fmt == S16) {
        if (h != 0xfffc && (h != 0xfff9 || op == VOID)) return;
        p += h == 0xfff9 ? 10 : 2;
    } else if (h == 0xfffb && op != VOID && !MUT(6)) {
        p += 10;
    }
    bool words = fmt == S16, fills = fmt != BASIC, void_basic = op == VOID && (fmt == BASIC || (fmt == S16 && !clip));
    int ps = words ? 2 : 1;
    RunFn run_fn = op == VOID ? nullptr : pick_run(op, alpha, words);
    int x_end = cx + cw, y_end = cy + ch, row = 0, x = 0;
    if (!clip) { cx = INT32_MIN; x_end = INT32_MAX; cy = INT32_MIN; y_end = INT32_MAX; }
    for (;;) {
        int w = u16(s, p);
        if (w == 0xffff) return;
        p += 2;
        if (w == 0xfffe) {
            if (++row >= y_end && !void_basic) return;
            d += row_step; x = 0;
        } else if ((w & 0x8000) != 0) {
            bool fill = fills && w > 0xbfff;
            int n = w & (fill ? 0x3fff : 0x7fff), skip = fill ? ps : n * ps;
            if (void_basic) {
                if (row < cy || row >= y_end) { d += n; x += n; p += skip; continue; }
                int r = rng_random(MUT(4) ? param : param + 1);
                for (; n > 0; n--, d++, x++) if (x >= cx && x < x_end) t[d] = t[d + r];
                p += skip;
                continue;
            }
            if (n == 0) { if (fill && words && !clip) p += 2; continue; }
            if (row < cy) { d += n * dx; x += n; p += skip; if (alpha && !MUT(5)) ap += n; continue; }
            if (op == VOID) {
                int r = rng_random(param + 1);
                for (; n > 0; n--, d++, x++) if (x >= cx && x < x_end) t[d] = t[d + r];
                p += skip;
                continue;
            }
            run_fn(t, s, d, p, ap, x, n, dx, fill, pal, cx, x_end, param);
            if (fill) p += ps;
        } else {
            d += w * dx; x += w;
        }
    }
}

static int enlarge_kernel(int s, int t, int a, bool add) {
    if (add) {
        int r = imin(((s >> 11) * a >> 8) + (t >> 11), 31), g = imin((((s >> 5) & 0x3f) * a >> 8) + ((t >> 5) & 0x3f), 63),
            b = imin(((s & 0x1f) * a >> 8) + (t & 0x1f), 31);
        return ushr(r << 16, 5) | ushr(g << 16, 11) | b;
    }
    int na = ~a & 0xff;
    return ((s >> 11) * a + (t >> 11) * na) >> 8 << 11 | (((s >> 5) & 0x3f) * a + ((t >> 5) & 0x3f) * na) >> 8 << 5
         | ((s & 0x1f) * a + (t & 0x1f) * na) >> 8;
}

void draw_enlarge(int fmt, bool clip, int d, int p, int pal, int stride, int cx, int cw, int cy, int ch, int k, int a, bool add) {
    uint16_t *t = PX();
    const uint8_t *s = BY();
    if (a > 255 || a == 0) return;
    int h = u16(s, p);
    if (fmt == EX) {
        if (h != 0xfffd && (h != 0xfffa || clip)) return;
        p += h == 0xfffa ? 10 : 2;
    } else if (fmt == S16) {
        if (h != 0xfffc && h != 0xfff9) return;
        p += h == 0xfff9 ? 10 : 2;
    } else if (h == 0xfffb) p += 10;
    bool words = fmt == S16, fills = fmt != BASIC;
    int ps = words ? 2 : 1;
    int x_end = cx + cw, y_end = cy + ch, y = 0, x = 0, row_start = d;
    if (!clip) { cx = INT32_MIN; x_end = INT32_MAX; cy = INT32_MIN; y_end = INT32_MAX; }
    for (;;) {
        int w = u16(s, p);
        if (w == 0xffff) return;
        p += 2;
        if (w == 0xfffe) {
            y += k;
            if (y >= y_end) return;
            row_start += stride * k; d = row_start; x = 0;
        } else if ((w & 0x8000) != 0) {
            bool fill = fills && w > 0xbfff;
            int n = w & (fill ? 0x3fff : 0x7fff), skip = fill ? ps : n * ps;
            if (n == 0) { if (fill && words && !clip) p += 2; continue; }
            if (y < cy) { d += n * k; p += skip; continue; }
            int bh = MUT(7) ? k : imin(k, y_end - y);
            for (int i = 0; i < n; i++, d += k, x += k) {
                int c = words ? u16(s, fill ? p : p + 2 * i) : t[pal + s[fill ? p : p + i]];
                if (x >= x_end || x < cx) continue;
                int bw = imin(k, x_end - x);
                for (int j = 0, o = d; j < bh; j++, o += stride)
                    for (int q = 0; q < bw; q++) t[o + q] = (uint16_t)enlarge_kernel(c, t[o + q], a, add);
            }
            p += skip;
        } else {
            d += w * k; x += w * k;
        }
    }
}

void draw_outline(bool clip, int d, int p, int pal, int pitch, int cx, int cw, int cy, int ch, int c) {
    uint16_t *t = PX();
    const uint8_t *s = BY();
    uint16_t col = (uint16_t)c;
    int x_end = cx + cw, y_end = cy + ch, row = 0, x = 0;
    for (;;) {
        int w = u16(s, p);
        if (w == 0xffff) return;
        p += 2;
        if (w == 0xfffe) {
            if (clip && ++row >= y_end) return;
            d += pitch; x = 0;
        } else if ((w & 0x8000) != 0) {
            int n = w & 0x7fff;
            if (!clip) {
                if (!MUT(8)) t[d - 1] = col;
                for (int j = 0; j < n; j++) t[d + j] = t[pal + s[p + j]];
                d += n; p += n;
                t[d] = col;
                continue;
            }
            if (row < cy) { d += n; p += n; continue; }
            bool edge = x < x_end && x > cx;
            if (edge) t[d - 1] = col;
            if (n != 0) {
                for (int j = 0; j < n; j++, x++) if (x < x_end && x >= cx) t[d + j] = t[pal + s[p + j]];
                d += n; p += n;
                edge = x_end > x && cx < x;
            }
            if (edge) t[d] = col;
        } else {
            d += w; x += w;
        }
    }
}

static void enlarge16(int d, int so, int w, int h, int ds, int ss, int k, int key, int a, bool add) {
    uint16_t *t = PX();
    const uint16_t *src = PX();
    if (a > 255 || a == 0 || h < 0) return;
    if (k <= 0) {
        int st = -k;
        for (int y = 0; y < h; y++, d += ds, so += st * ss) {
            if (w < 0) continue;
            for (int x = 0; x <= w; x++) {
                int c = src[so + x * st];
                if (c != key) t[d + x] = (uint16_t)enlarge_kernel(c, t[d + x], a, add);
            }
        }
        return;
    }
    if (w < 0) return;
    for (int y = 0; y < h; y += k, d += ds * k, so += ss) {
        int bh = imin(k, h - y);
        for (int x = 0, q = 0; x < w; x += k, q++) {
            int c = src[so + q], bw = imin(k, w - x);
            if (c == key) continue;
            for (int j = 0, o = d + x; j < bh; j++, o += ds)
                for (int e = 0; e < bw; e++) t[o + e] = (uint16_t)enlarge_kernel(c, t[o + e], a, add);
        }
    }
}

void draw16(int op, int d, int so, int w, int h, int ds, int ss, int param, int key, int a, bool add) {
    uint16_t *t = PX();
    const uint16_t *src = PX();
    if (h == 0) return;
    if (op == ENLARGE) { enlarge16(d, so, w, h, ds, ss, param, key, a, add); return; }
    int dx = 1;
    if (op == FLIP) {
        if (param < 0 || param > 2) return;
        if (param != 1) { d += w; dx = -1; }
        if (param != 0) { d += h * ds; ds = -ds; }
        op = COPY;
    } else if (op == LIGHTING && param == 16) op = COPY;
    else if (op == RGBADD) { if (param == 0) return; }
    else if (!active(op, param)) return;
    for (int y = 0; y < h; y++, d += ds, so += ss) {
        if (op == VOID) {
            int r = rng_random(param + 1);
            for (int x = 0; x < w; x++) if (src[so + x] != key) t[d + x] = t[d + x + r];
            continue;
        }
        if (op == GRAY && param >= 1 && param <= 15) {
            int s0 = w > 0 ? src[so] : 0;
            for (int x = 0; x < w; x++) {
                int c = src[so + x];
                if (c == key) continue;
                int v = (c >> 10) + 3 + ((c & 15) << 2) + ((c >> 4) & 0x3f), gy = (v >> 4) + (v >> 2) + (v >> 6);
                t[d + x] = (uint16_t)(mix_i(MUT(9) ? c : s0, 16 - param) + mix_i((gy << 10 | gy << 4 | gy >> 2) & 0xffff, param));
            }
            continue;
        }
        for (int x = 0, o = d; x < w; x++, o += dx) {
            int c = src[so + x];
            if (c == key) continue;
            if (op == RGBADD) {
                int r = imin((c >> 11) + (param >> 11), 31), b = imin((c & 0x1f) + (param & 0x1f), 31),
                    g = imin(((c >> 5) & 0x3f) + ((param >> 5) & 0x3f), 63);
                t[o] = (uint16_t)(ushr(r << 16, 5) | b | g << 5);
            } else t[o] = (uint16_t)kernel(op, c, t[o], param);
        }
    }
}

}  // namespace nat
}  // namespace emu
