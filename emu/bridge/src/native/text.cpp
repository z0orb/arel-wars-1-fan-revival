// The .ft2 text: a C++ port of universal/engine/src/arel/engine/gfx/Text.java (spec/ft2.md,
// "Drawing text"). CGxBFont::DrawText with DrawFont, DrawEngFont, DrawHan, CombineHan, KS2TG and
// the game's parser (CPdGraphics::DoParseForCalc, DoParseForDraw) as host code.
// All state is in guest memory: the CGxBFont, its font files, the parser object and the library
// tables. The port reads the tables from the library bytes, not from Java tables.
// Only the game's font is ported: HAN type 1 (not BFT), variant type 0, no proportional ENG
// table, no platform font, no ISO-8859-1 font. Another font stops the bridge.
#include <initializer_list>
#include "native.h"

namespace emu {
namespace nat {

static constexpr int W = 480;
static constexpr uint32_t GOT = 0x120d68;  // VA

namespace {

inline int s8(uint32_t a) { return (int8_t)R8(a); }

// Library tables (read live through their GOT slots, which are symbol relocations).
struct Tables {
    uint32_t ks, jamo, idx, med, row, rgb, vtable, calc, draw;
    Tables() {
        ks = R32(VA(GOT + 0x544));
        jamo = R32(VA(GOT + 0x310));
        idx = R32(VA(GOT + 0xbb0));
        med = R32(VA(GOT + 0x698));
        row = R32(VA(GOT + 0x478));
        rgb = GHIDRA(0x11c02c);
        vtable = GHIDRA(0x12ef10);
        calc = so_sym("_ZThn4_N11CPdGraphics14DoParseForCalcEi");
        draw = so_sym("_ZThn4_N11CPdGraphics14DoParseForDrawEiP8CGxBFont");
        if (!calc || !draw) die("native: text: no parser thunks");
    }
};
const Tables &tables() { static Tables t; return t; }

// The game's parser (CGxBFontParser at CPdGraphics + 4): draw state +0xc, calc state +0x10.
struct Parser {
    uint32_t p;
    int calc(int c) const {
        uint32_t a = p + 0x10;
        int st = (int32_t)R32(a);
        if (c == '{' && st == 0) { W32(a, 1); return 1; }
        if (st > 0) {
            if (c > 0x60 && st == 1 && c < 0x7b) { W32(a, 2); return 1; }
            if (c == '}') { W32(a, 0); return 1; }
        }
        return 0;
    }
    int draw(int c, uint32_t font) const {
        uint32_t a = p + 0xc;
        int st = (int32_t)R32(a);
        if (c == '{' && st == 0) { W32(a, 1); return 1; }
        if (st > 0) {
            if (c > 0x60 && st == 1 && c < 0x7b) {
                int k = c - 0x61;
                if (k < 13) {
                    uint32_t t = tables().rgb + 3 * (uint32_t)k;
                    W32(font + 0x58, (uint32_t)pixel_from_rgb(R8(t), R8(t + 1), R8(t + 2)));
                } else W32(font + 0x58, MUT(32) ? 0 : 0xffff);
                W32(a, 2);
                return 1;
            }
            if (c == '}') { W32(font + 0x58, 0xffff); W32(a, 0); return 1; }
        }
        return 0;
    }
};

struct Font {
    uint32_t f, obj;
    int px, y_off, scr_h;
    int u(int off) const { return R8(f + (uint32_t)off); }
    int eng_w() const { return u(0x66); }
    int eng_h() const { return u(0x67); }
    int han_w() const { return u(0x64); }
    int han_h() const { return u(0x65); }
    int extra_w() const { return u(0x68); }
    int extra_h() const { return u(0x69); }
    int max_h() const { return u(0x70); }

    // DrawFont(x, y, w, h, glyph): a 1-bit stream, MSB first, w bits per row.
    void draw_font(int x, int y, int w, int h, uint32_t g) const {
        if (!obj) return;
        int cx0 = (int16_t)R16(f + 0x48), top = (int16_t)R16(f + 0x4a);
        int cw = (int16_t)R16(f + 0x4c), ch = (int16_t)R16(f + 0x4e);
        int skip, left;
        y += y_off;
        if (y < top) {
            int u = w * (top - y);
            g += (uint32_t)(u >> 3); skip = u & 7; left = 8 - skip;
            h += y - top; y = top;
        } else { skip = 0; left = 8; }
        if (top + ch < h + y) h = top + ch - y;
        int right = x + w, cx_end = cx0 + cw;
        if (h <= 0 || cx0 > right || x > cx_end) return;
        int end = h + y, acc = R8(g) << skip;
        uint32_t p = g + 1;
        uint16_t c = (uint16_t)R32(f + 0x58), *t = PX();
        for (int row = y; row < end; row++) {
            if (x >= right) continue;
            int d = px + row * W + x;
            for (int col = x; col < right;) {
                if (col < cx_end && cx0 <= col && (acc & 0x80)) t[d] = c;
                acc <<= 1;
                if (--left == 0) { acc = R8(p++); left = MUT(33) ? 7 : 8; }
                col++; d++;
            }
        }
    }

    void draw_eng(int x, int y, int i) const { draw_font(x, y, eng_w(), eng_h(), R32(f + 0x28) + (uint32_t)(u(0x72) * i)); }

    // CombineHan(src, s): scratch[j] |= low byte of (acc >> s), acc the stream so far.
    void combine(uint32_t scratch, uint32_t src, int s) const {
        int n = u(0x71), acc = 0;
        s &= 0xff;
        for (int j = 0; j < n; j++) {
            acc = acc << 8 | R8(src + (uint32_t)j);
            W8(scratch + (uint32_t)j, (uint8_t)(R8(scratch + (uint32_t)j) | (uint8_t)(s >= 32 ? acc >> 31 : acc >> s)));
        }
    }

    // DrawHan(x, y, I, M, F): one Hangul syllable from Johab parts, combined from jamo glyphs.
    void draw_han(int x, int y, int ci, int cm, int cf) const {
        if (ci == 0 && cm == 0 && cf == 0) return;
        const Tables &tb = tables();
        auto med = [&](int i) { return s8(tb.med + (uint32_t)i); };  // med(-1) is the byte before the table
        auto row0 = [&](int j) { return s8(tb.row + (uint32_t)j); }; // variant type 0
        int i = s8(tb.idx + (uint32_t)ci), m = s8(tb.idx + 0x20u + (uint32_t)cm), fi = s8(tb.idx + 0x40u + (uint32_t)cf);
        int vi, vm, vf, k;
        if (fi >= 0) {
            if (med(21 + m) == 1) {
                int a = s8(R32(f + 0x44) + (uint32_t)fi), b = s8(R32(f + 0x3c) + (uint32_t)imax(i, 0)), p, q, r;
                if (a + b >= (MUT(34) ? 4 : 5)) { p = 0; q = 1; r = 2; }
                else if (b == 1 && a == 3) { p = 3; q = 4; r = 5; }
                else if (b == 3 && a == 1) { p = 6; q = 7; r = 8; }
                else { p = 9; q = 10; r = 11; }
                vi = row0(p) + med(42 + m); vm = row0(q); vf = row0(r);
            } else { vi = row0(12); vm = row0(13); vf = 0; }
            k = m;
        } else if (m >= 0) {
            int t = med(m);
            vi = t >= 1 ? t - 1 : t; vm = 0; vf = 0; k = m;
        } else { vi = med(0); vm = 0; vf = 0; k = 0; }
        if (i < 0 && fi < 0) vm = 6;
        int n = u(0x71);
        uint32_t scratch = R32(f + 0x24);
        memset(P(scratch, (uint32_t)n), 0, (size_t)n);
        if (i >= 0) combine(scratch, R32(f + 0x18) + (uint32_t)(n * (vi * 19 + i)), m < 0 && fi < 0 ? 2 : 0);
        if (m >= 0) combine(scratch, R32(f + 0x1c) + (uint32_t)(n * (vm * 21 + m)), 0);
        if (fi >= 0) combine(scratch, R32(f + 0x20) + (uint32_t)((vf * 27 + fi) * n), s8(R32(f + 0x40) + (uint32_t)k));
        draw_font(x, y, han_w(), han_h(), scratch);
    }
};

// KS2TG: KS X 1001 code to Johab. It reads the library tables as the original does, also for a
// lo byte outside 0xa1..0xfe (the neighbouring entries).
int ks2tg(int code) {
    const Tables &tb = tables();
    int hi = code >> 8 & 0xff, lo = code & 0xff;
    if ((uint32_t)(hi - 0xb0) <= 0x18u) return R16(tb.ks + 2u * (uint32_t)((hi - 0xb0) * 94 + lo - 0xa1));
    if (hi == 0xa4) return (uint32_t)(lo - 0xa1) <= 0x32u ? R16(tb.jamo + 2u * (uint32_t)(lo - 0xa1)) : -1;
    if (hi == 0xa3) return (uint32_t)(lo - 0xb0) <= 8u ? ((lo - 0xa5) | 0xfc00) : -1;
    if (hi == 0xa8) return (uint32_t)(lo - 0xcd) <= (MUT(35) ? 8u : 9u) ? ((lo - 0xcc) | 0xfc00) : -1;
    return code;
}

}  // namespace

// CGxBFont::DrawText(this, s, len, x, y, wrap, skipSpace, ks).
void draw_text(uint32_t f, uint32_t s, int len, int x, int y, int wrap, bool skip_space, bool ks) {
    if (!s) return;
    if (R32(f + 0x80)) die("native: DrawText with a platform font (not ported)");
    if (R32(f + 0xc) && !R8(f + 0x38)) die("native: DrawText with an ISO-8859-1 font (not ported)");
    // The pointer fixups at the start of the original DrawText.
    if (uint32_t e = R32(f + 4)) W32(f + 0x28, e + 2);
    if (uint32_t e = R32(f + 8)) W32(f + 0x30, e + 2);
    if (uint32_t hf = R32(f)) {
        uint32_t base = hf + R8(f + 0x76), old = R32(f + 0x18);
        if (old != base) {
            uint32_t d = base - old;
            W32(f + 0x18, base);
            for (uint32_t o : {0x1cu, 0x20u, 0x3cu, 0x40u, 0x44u}) W32(f + o, R32(f + o) + d);
        }
    }
    if (uint32_t sc = R32(f + 0x14)) W32(f + 0x24, sc);
    if (R8(f + 0x74) != 1 || R8(f + 0x75) != 0 || R32(f + 0x2c) || !R32(f + 0x14))
        die("native: DrawText font type %u/%u, ENG table 0x%x, scratch 0x%x (only the game font is ported)",
            R8(f + 0x74), R8(f + 0x75), R32(f + 0x2c), R32(f + 0x14));
    Parser ps{R32(f + 0x7c)};
    if (ps.p) {
        const Tables &tb = tables();
        uint32_t vt = R32(ps.p);
        if (vt != tb.vtable || R32(vt) != tb.calc || R32(vt + 4) != tb.draw)
            die("native: DrawText parser vtable 0x%x (0x%x, 0x%x), not CPdGraphics 0x%x", vt, R32(vt), R32(vt + 4), tb.vtable);
    }
    if (len < 0) { len = 0; while (R8(s + (uint32_t)len)) len++; }
    int right = wrap > 0 ? wrap + x : wrap;
    int scr_h = (int32_t)R32(f + 0x54);
    if (len <= 0 || y >= scr_h) return;
    Font ft{f, R32(f + 0x10), 0, 0, scr_h};
    if (ft.obj) {
        uint32_t surf = R32(ft.obj);
        if ((int32_t)R32(surf) != W || R32(surf + 0xc) != 16)
            die("native: text surface is %d wide with %u bits per pixel (only 480 and 16 are ported)", (int32_t)R32(surf), R32(surf + 0xc));
        ft.px = pix(R32(surf + 0x10));
        ft.y_off = (int32_t)R32(ft.obj + 0x44);
    }
    int eng_w = ft.eng_w(), eng_h = ft.eng_h(), max_h = ft.max_h();
    int ci = 0, cm = 0, cf = 0, cx = x;
    uint32_t p = s;
    auto b = [](uint32_t a) { return (int)R8(a); };
    while (true) {
        int hs = ft.u(0x6a), vs = ft.u(0x6b);
        int c = b(p);
        if (ps.p) {
            ps.calc(c);
            if (ps.draw(c, f) != 0) { len--; p++; if (len < 1 || scr_h <= y) return; continue; }
        }
        if (c == 10) { y += vs + max_h; cx = x; len--; p++; }
        else if (c < 0x80) {
            if (eng_w == 0) { len--; p++; }
            else if (c - 0x21 >= 0 && c - 0x21 < 0x5e) {
                int run = 0, n = 0;
                do {
                    if (ps.p && ps.calc(b(p + (uint32_t)run)) != 0) n++;
                    run++;
                } while (((b(p + (uint32_t)run) - 0x21) & 0xff) < 0x5e);
                if (len < run) run = len;
                if (x < cx && right > 0 && right < (run - n) * (hs + eng_w) + (cx - hs)) { y += vs + max_h; cx = x; }
                len -= run;
                int ey = eng_h < max_h ? max_h - eng_h : 0;
                for (int j = 0; j < run; j++) {
                    int ch = b(p + (uint32_t)j);
                    if (ps.p && ps.draw(ch, f) != 0) continue;
                    ft.draw_eng(cx, y + ey, ch - 0x21);
                    cx += hs + eng_w;
                }
                p += (uint32_t)run;
            } else {
                if (!(cx <= x && skip_space)) cx += hs + eng_w;
                len--; p++;
            }
        } else {
            int code = c << 8 | b(p + 1), cw;
            p += 2; len -= 2;
            if (code - 0xa1a4 >= 0 && code - 0xa1a4 < 2) { code = code == 0xa1a4 ? 0x2e : 0x3a; cw = eng_w; }
            else {
                if (ks) code = ks2tg(code);
                ci = code >> 10 & 31; cm = code >> 5 & 31; cf = code & 31;
                cw = ci == 31 ? ft.extra_w() : ft.han_w();
            }
            if (cw != 0) {
                if (right > 0 && right < cw + cx) { y += vs + max_h; cx = x; }
                if (code != -1) {
                    if (code - 0x21 >= 0 && code - 0x21 < 0x5e) ft.draw_eng(cx, y + (eng_h < max_h ? max_h - eng_h : 0), (code & 0xff) - 0x21);
                    else if (ci == 31) {
                        int eh = ft.extra_h();
                        ft.draw_font(cx, y + (eh < max_h ? max_h - eh : 0), ft.extra_w(), eh, R32(f + 0x30) + (uint32_t)(ft.u(0x73) * (cf - 1)));
                    } else ft.draw_han(cx, y + (ft.han_h() < max_h ? max_h - ft.han_h() : 0), ci, cm, cf);
                }
                cx += cw + hs;
            }
        }
        if (len < 1 || scr_h <= y) return;
    }
}

}  // namespace nat
}  // namespace emu
