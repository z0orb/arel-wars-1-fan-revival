// PutCompressImg, the fades and the waves: a C++ port of universal/engine/src/arel/engine/gfx/Screen.java
// (spec/render.md). The state that Screen.java keeps in fields is read from the guest screen object:
// clip rect (s16 x, y, w, h at +0x48), y offset (+0x44), ENLARGE weight (+0x24), additive mode (+0x50),
// surface (word 0: +0 width, +0xc bits per pixel, +0x10 pixels).
#include "native.h"

namespace emu {
namespace nat {

static constexpr int W = 480, H = 320;

namespace {
struct Scr {
    uint32_t obj;
    int clip_x, clip_y, clip_w, clip_h, y_off, weight, add_mode, px;
    Scr() {
        obj = screen_obj();
        uint32_t surf = R32(obj);
        if ((int32_t)R32(surf) != W || R32(surf + 0xc) != 16)
            die("native: the main screen is %d wide with %u bits per pixel (only 480 and 16 are ported)", (int32_t)R32(surf), R32(surf + 0xc));
        clip_x = (int16_t)R16(obj + 0x48); clip_y = (int16_t)R16(obj + 0x4a);
        clip_w = (int16_t)R16(obj + 0x4c); clip_h = (int16_t)R16(obj + 0x4e);
        y_off = (int32_t)R32(obj + 0x44); weight = (int32_t)R32(obj + 0x24); add_mode = (int32_t)R32(obj + 0x50);
        px = pix(R32(surf + 0x10));
    }
};
}  // namespace

static void put_on(const Scr &sc, int x, int y, int w, int h, int s, int pal, int op, int param, int t_base) {
    int flip = -1;
    switch (op) {
    case ENLARGE:
        if (param <= 1) op = COPY; else { w *= param; h *= param; }
        break;
    case FLIP: flip = param; break;
    case 20: flip = param & 0xffff; w *= ushr(param, 16); h *= ushr(param, 16); break;
    case 21: case 22: return;
    default: break;
    }
    int cx = 0, cw = w, cy = 0, ch = h;
    bool clip = false;
    if (flip >= -1 && flip <= 2) {
        int l = sc.clip_x, r = sc.clip_x + sc.clip_w, top = sc.clip_y, bot = sc.clip_y + sc.clip_h;
        if (flip == -1 || flip == 1) {
            if (x < l) { cx = l - x; cw = w - cx; clip = true; }
            if (x + w > r) { cw = r - x - cx; clip = true; }
        } else {
            if (l > x) { cw = x - l + (MUT(12) ? 0 : 1) + w; clip = true; }
            if (x + w >= r) { cx = x + w - r + 1; cw -= cx; clip = true; }
        }
        if (flip == -1 || flip == 0) {
            if (y < top) { cy = top - y; ch = h - cy; clip = true; }
            if (y + h > bot) { ch = bot - y - cy; clip = true; }
        } else {
            if (y < top) { ch = y - top + 1 + h; clip = true; }
            if (y + h >= bot) { cy = y + h - bot + 1; ch -= cy; clip = true; }
        }
    }
    if (ch <= 0 || cw <= 0) return;
    if (!MUT(11)) y += sc.y_off;
    if (op == 20 || flip < -1 || flip > 2) return;
    int d = t_base + y * W + x, pitch;
    switch (flip) {
    case -1: pitch = op == ENLARGE ? W : W - w; break;
    case 0: pitch = W + w; d += w; break;
    case 1: pitch = W + w; d += W * h; break;
    default: pitch = W - w; d += W * h + w; break;
    }
    if (op < 0 || op > 22) return;
    int p = param;
    if ((op >= 1 && op <= 5) || (op >= 16 && op <= 19)) p = param & 0xff;
    else if (op >= 11 && op <= 15) p = param & 0xffff;
    switch (op) {
    case 5: case 7: case 8: case 9: case 21: case 22: return;
    case OUTLINE:
        put_on(sc, x, y - sc.y_off - 1, w, h, s, pal, RGB, p, t_base);
        if (!MUT(13)) put_on(sc, x, y - sc.y_off + 1, w, h, s, pal, RGB, p, t_base);
        draw_outline(clip, d, s, pal, pitch, cx, cw, cy, ch, p);
        return;
    case ENLARGE: {
        int fmt = auto_format(s);
        if (fmt >= ALPHA) fmt = fmt == ALPHA ? BASIC : fmt == EX_ALPHA ? EX : S16;
        draw_enlarge(fmt, clip, d, s, pal, pitch, cx, cw, cy, ch, p, sc.weight, sc.add_mode != 0);
        return;
    }
    default:
        draw_auto(op, clip, d, s, pal, pitch, cx, cw, cy, ch, p);
    }
}

void put(int x, int y, int w, int h, int s, int pal, int op, int param, uint32_t target) {
    Scr sc;
    put_on(sc, x, y, w, h, s, pal, op, param, target ? pix(R32(target + 0x10)) : sc.px);
}

void rgb_blend16(int d, int pitch_bytes, int w, int h, int c, int level) {
    uint16_t *t = PX();
    if ((level < 0 && !MUT(14)) || level > 15 || h == 0) return;
    int pitch = pitch_bytes >> 1, cc = level == 15 ? c : mix(c, level + 1);
    for (int y = 0; y < h; y++, d += pitch)
        for (int j = 0; j < w; j++) t[d + j] = (uint16_t)(level == 15 ? c : cc + mix(t[d + j], 15 - level));
}

void fade_out16(int d, int pitch_bytes, int w, int h, int level) {
    uint16_t *t = PX();
    if (level < 0 || level > 15 || h == 0) return;
    int pitch = pitch_bytes >> 1;
    for (int y = 0; y < h; y++, d += pitch)
        for (int j = 0; j < w; j++) t[d + j] = (uint16_t)mix(t[d + j], MUT(15) ? level + 1 : level);
}

// The block of a Screen_*_Real call (Screen.realRect). False when w or h ends up negative.
static bool real_rect(const Scr &sc, int &x, int &y, int &w, int &h) {
    if (sc.clip_x > x) { w += x - sc.clip_x; x = sc.clip_x; }
    if (w + x > sc.clip_x + sc.clip_w) w = sc.clip_x + sc.clip_w - x;
    if (sc.clip_y > y) { h += y - sc.clip_y; y = sc.clip_y; }
    if (h + y > sc.clip_y + sc.clip_h) h = sc.clip_y + sc.clip_h - y;
    if (h < 0 || w < 0) return false;
    x = imax(imin(x, W - 1), 0);
    y = MUT(16) ? y + sc.y_off : imax(imin(y + sc.y_off, H - 1), 0);
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    return true;
}

void rgb_blend_real(int x, int y, int w, int h, int colour, int level) {
    if (level < 0 || level > 15) return;
    Scr sc;
    if (real_rect(sc, x, y, w, h)) rgb_blend16(sc.px + y * W + x, 2 * W, w, h, colour, level);
}

void fade_out_real(int x, int y, int w, int h, int level) {
    if (level < 0 || level > 15) return;
    Scr sc;
    if (real_rect(sc, x, y, w, h)) fade_out16(sc.px + y * W + x, 2 * W, w, h, level);
}

void wave_h16(int d, int pitch_bytes, int w, int h, int tab, int n) {
    uint16_t *t = PX();
    const int8_t *tb = reinterpret_cast<const int8_t *>(BY());
    if (h == 0) return;
    int pitch = pitch_bytes >> 1;
    for (int k = h - 1;; k--, d += pitch) {
        int off = tb[tab + (MUT(17) ? (h - 1 - k) : k) % n];
        if (off != 0) memmove(t + d, t + d + off, 2 * (size_t)w);
        if (k == 0) return;
    }
}

void wave_v16(int d, int pitch_bytes, int w, int h, int tab, int n) {
    uint16_t *t = PX();
    const int8_t *tb = reinterpret_cast<const int8_t *>(BY());
    int pitch = pitch_bytes >> 1, top = d, bot = d + pitch * h;
    for (int c = w; c != 0; top++, bot++) {
        c--;
        int off = tb[tab + c % n];
        if (off == 0 || h == 0) continue;
        int dd = off * pitch;
        if (dd > 0) for (int i = 0, q = top; i < h; i++, q += pitch) t[q] = t[q + dd];
        else if (MUT(18)) for (int i = 0, q = top; i < h; i++, q += pitch) t[q] = t[q + dd];
        else for (int i = 0, q = bot; i < h; i++, q -= pitch) t[q] = t[q + dd];
    }
}

void wave_real(int x, int y, int w, int h, int tab, int n, int vertical) {
    x = imin(x, W - 1);
    if (x < 0) x = 0;
    y = imin(y, H - 1);
    if (y < 0) y = 0;
    if (x + w > W) w -= x + w - W;
    if (y + h > H) h -= y + h - H;
    if (h <= 0 || w <= 0) return;
    // The global kernel pointers (Ghidra 0x633868 and 0x63386c). The port runs only if each one
    // holds its Screen_WAVE*_16 function. A null pointer does nothing, as in the guest.
    bool v = (vertical & 0xff) != 0;
    uint32_t fp = R32(GHIDRA(v ? 0x63386c : 0x633868));
    if (!fp) return;
    static uint32_t want_h, want_v;
    if (!want_h) { want_h = so_sym("_Z15Screen_WAVEH_16PtiiiPai"); want_v = so_sym("_Z15Screen_WAVEV_16PtiiiPai"); }
    if (fp != (v ? want_v : want_h)) die("native: the wave pointer is 0x%x, not Screen_WAVE%c_16 0x%x", fp, v ? 'V' : 'H', v ? want_v : want_h);
    Scr sc;
    if (!v) wave_h16(sc.px + y * W + x, 2 * W, w, h, tab, n);
    else wave_v16(sc.px + y * W + x, 2 * W, w, h, tab, n);
}

}  // namespace nat
}  // namespace emu
