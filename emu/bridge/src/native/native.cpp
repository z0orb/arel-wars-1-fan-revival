// The registry of native replacements (PLAN.md DES-12): the hooks, the argument decoding, the switches.
#include <cstdlib>
#include <ctime>
#include <vector>
#include "native.h"

namespace emu {
namespace nat {

int g_mut;

int pix(uint32_t addr) {
    if (addr & 1u) die("native: odd pixel pointer 0x%x", addr);
    return (int)(addr >> 1);
}

int rng_random(int n) {
    if (n <= 0) return 0;
    uint32_t *st = reinterpret_cast<uint32_t *>(g_base + VA(RNG_STATE));
    uint32_t s = *st * 214013u + 2531011u;
    *st = s;
    return (int)(((s & 0x7fffffffu) >> 16) % (uint32_t)n);
}

uint32_t screen_obj() {
    uint32_t frame = R32(VA(FRAME_PTR));
    if (!frame) die("native: no frame object yet");
    return frame + 8;
}

namespace {

struct Leaf { const char *name; int op, fmt, clip, ints, u16param; };
const Leaf LEAVES[] = {
#include "leaves.inc"
};
constexpr int NLEAVES = sizeof LEAVES / sizeof LEAVES[0];

int I(int i) { return (int32_t)arg(i); }

void leaf(const Leaf &l) {
    if (l.fmt == 7) {  // DrawOP_X_16(dst, src, w, h, dstStride, srcStride, [param,] key)
        bool np = l.ints == 5;
        int param = np ? 0 : I(6), key = I(np ? 6 : 7);
        uint32_t so = screen_obj();
        draw16(l.op, pix(arg(0)), pix(arg(1)), I(2), I(3), I(4), I(5), param, key, (int32_t)R32(so + 0x24), R32(so + 0x50) != 0);
        return;
    }
    // (dst, data, palette, pitch, [cx, cw, cy, ch,] [param])
    int d = pix(arg(0)), p = (int)arg(1), pal = pix(arg(2)), pitch = I(3), k = 4, cx = 0, cw = 0, cy = 0, ch = 0;
    if (l.clip) { cx = I(k++); cw = I(k++); cy = I(k++); ch = I(k++); }
    int param = l.ints > (l.clip ? 5 : 1) ? I(k) : l.u16param ? I(k) & 0xffff : 0;
    if (l.op == OUTLINE) { draw_outline(l.clip, d, p, pal, pitch, cx, cw, cy, ch, param); return; }
    if (l.op == ENLARGE) {
        int fmt = l.fmt == 6 ? auto_format(p) : l.fmt;
        if (fmt >= ALPHA) fmt = fmt == ALPHA ? BASIC : fmt == EX_ALPHA ? EX : S16;
        uint32_t so = screen_obj();
        draw_enlarge(fmt, l.clip, d, p, pal, pitch, cx, cw, cy, ch, param, (int32_t)R32(so + 0x24), R32(so + 0x50) != 0);
        return;
    }
    if (l.fmt == 6) draw_auto(l.op, l.clip, d, p, pal, pitch, cx, cw, cy, ch, param);
    else draw(l.op, l.fmt, l.clip, d, p, pal, pitch, cx, cw, cy, ch, param);
}

// PutCompressImg(x, y, w, h, data, palette, op, param, target)
void put_compress_img() { put(I(0), I(1), I(2), I(3), (int)arg(4), pix(arg(5)), I(6), I(7), arg(8)); }
// CGxPZxZero::Blt and BltFast(this, x, y, w, h, unused, unused, op, param, target)
void blt() {
    uint32_t img = arg(0);
    int w = I(3), h = I(4);
    if (w == -1 && !MUT(19)) w = (int16_t)R16(img + 8);
    if (h == -1) h = (int16_t)R16(img + 0xa);
    // CGxPalette16::operator unsigned short*: the palette object's +8
    put(I(1), I(2), w, h, (int)R32(img + 0x1c), pix(R32(R32(img + 0x20) + 8)), I(7), I(8), arg(9));
}
void rgbblend16() { rgb_blend16(pix(arg(0)), I(1), I(2), I(3), I(4), I(5)); }
void fadeout16() { fade_out16(pix(arg(0)), I(1), I(2), I(3), I(4)); }
void rgbblend_real() { rgb_blend_real(I(0), I(1), I(2), I(3), I(4), I(5)); }
void fadeout_real() { fade_out_real(I(0), I(1), I(2), I(3), I(4)); }
void waveh16() { wave_h16(pix(arg(0)), I(1), I(2), I(3), (int)arg(4), I(5)); }
void wavev16() { wave_v16(pix(arg(0)), I(1), I(2), I(3), (int)arg(4), I(5)); }
void wave_r() { wave_real(I(0), I(1), I(2), I(3), (int)arg(4), I(5), I(6)); }
// MC_grpFillRect(fb, x, y, w, h, ctx) and MC_grpPutPixel(fb, x, y, ctx): pixels at fb + 0x10,
// colour at ctx + 0x14.
void mc_fill() { mc_fill_rect(pix(R32(arg(0) + 0x10)), I(1), I(2), I(3), I(4), (int32_t)R32(arg(5) + 0x14)); }
void mc_pixel() { mc_put_pixel(pix(R32(arg(0) + 0x10)), I(1), I(2), (int32_t)R32(arg(3) + 0x14)); }
void g_clear() { prim_clear_screen(arg(0), I(1)); }
void g_linedda() { prim_line_dda(arg(0), I(1), I(2), I(3), I(4)); }
void g_line() { prim_draw_line(arg(0), I(1), I(2), I(3), I(4), I(5)); }
void g_rect() { prim_draw_rect(arg(0), I(1), I(2), I(3), I(4), I(5)); }
void g_fillrect() { prim_draw_fill_rect(arg(0), I(1), I(2), I(3), I(4), I(5)); }
void g_roundrect() { prim_draw_fill_round_rect(arg(0), I(1), I(2), I(3), I(4), I(5), I(6)); }
// CGxBFont::DrawText(this, s, len, x, y, wrap, bool skipSpace, bool ks): the bools are bytes.
void g_text() { draw_text(arg(0), arg(1), I(2), I(3), I(4), I(5), (arg(6) & 0xff) != 0, (arg(7) & 0xff) != 0); }
void g_ellipse() { prim_fill_ellipse(arg(0), I(1), I(2), I(3), I(4), I(5), I(6)); }

struct Fn { const char *name, *family; void (*run)(); };
const Fn FNS[] = {
    {"_Z14PutCompressImgiiiiPhPt10enumDrawOPil", "PutCompressImg", put_compress_img},
    {"_ZN10CGxPZxZero3BltEiiiiii10enumDrawOPil", "Blt", blt},
    {"_ZN10CGxPZxZero7BltFastEiiiiii10enumDrawOPil", "Blt", blt},
    {"_Z20Screen_RGBBLEND16_16Ptiiiii", "fade", rgbblend16},
    {"_Z19Screen_FADEOUT16_16Ptiiii", "fade", fadeout16},
    {"_Z22Screen_RGBBLEND16_Realiiiiji", "fade", rgbblend_real},
    {"_Z21Screen_FADEOUT16_Realiiiii", "fade", fadeout_real},
    {"_Z15Screen_WAVEH_16PtiiiPai", "wave", waveh16},
    {"_Z15Screen_WAVEV_16PtiiiPai", "wave", wavev16},
    {"_Z17Screen_WAVEH_RealiiiiPaih", "wave", wave_r},
    {"_Z17Screen_WAVEV_RealiiiiPaih", "wave", wave_r},
    {"MC_grpFillRect", "prim", mc_fill},
    {"MC_grpPutPixel", "prim", mc_pixel},
    {"_ZN11CGsGraphics11ClearScreenEj", "prim", g_clear},
    {"_ZN11CGsGraphics17LineDDAFixedPointEiiii", "prim", g_linedda},
    {"_ZN11CGsGraphics8DrawLineEiiiij", "prim", g_line},
    {"_ZN11CGsGraphics8DrawRectEiiiij", "prim", g_rect},
    {"_ZN11CGsGraphics12DrawFillRectEiiiij", "prim", g_fillrect},
    {"_ZN11CGsGraphics17DrawFillRoundRectEiiiiij", "prim", g_roundrect},
    {"_ZN11CGsGraphics21DrawNativeFillEllipseEiiiij17EnumGsDegreeCCW90", "prim", g_ellipse},
    {"_ZN8CGxBFont8DrawTextEPKciiiibb", "text", g_text},
};
constexpr int NFNS = sizeof FNS / sizeof FNS[0];

// Index n: 0 to NFNS - 1 the functions above, then the leaves.
std::vector<uint64_t> g_calls(NFNS + NLEAVES), g_ns(NFNS + NLEAVES);
std::vector<bool> g_on(NFNS + NLEAVES);
bool g_timing;  // AREL_NATIVE_TIME set: the time per replacement goes into the report
uint64_t now_ns() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }

const char *name_of(int n) { return n < NFNS ? FNS[n].name : LEAVES[n - NFNS].name; }
const char *family_of(int n) { return n < NFNS ? FNS[n].family : "leaves"; }

// True if the entry e of the spec names function n.
bool names(const char *e, size_t len, int n) {
    auto eq = [&](const char *s) { return strlen(s) == len && !strncmp(s, e, len); };
    return eq("all") || eq(name_of(n)) || eq(family_of(n));
}

}  // namespace

int total() { return NFNS + NLEAVES; }

int install(const char *spec) {
    if (!spec) spec = "all";
    const char *m = getenv("AREL_NATIVE_MUT");
    g_mut = m ? atoi(m) : 0;
    g_timing = getenv("AREL_NATIVE_TIME") != nullptr;
    int on = 0;
    for (int n = 0; n < NFNS + NLEAVES; n++) {
        bool sel = false;
        for (const char *p = spec; *p;) {
            const char *q = strchr(p, ','); size_t len = q ? (size_t)(q - p) : strlen(p);
            bool neg = *p == '-';
            if (neg ? names(p + 1, len - 1, n) : names(p, len, n)) sel = !neg;
            p += len; if (*p == ',') p++;
        }
        if (!sel) continue;
        uint32_t a = so_sym(name_of(n));
        if (!a) die("native: symbol %s not found", name_of(n));
        g_core->hook_replace(a, SVC_NATIVE + (uint32_t)n);
        g_on[n] = true;
        on++;
    }
    g_core->clear_cache();
    olog("native: %d of %d replacements on (spec \"%s\")%s", on, NFNS + NLEAVES, spec, g_mut ? ", MUTATION ON" : "");
    if (g_mut) olog("native: mutation %d", g_mut);
    return on;
}

bool svc(uint32_t n) {
    if (n >= (uint32_t)(NFNS + NLEAVES) || !g_on[n]) return false;
    g_calls[n]++;
    uint64_t t0 = g_timing ? now_ns() : 0;
    if (n < (uint32_t)NFNS) FNS[n].run();
    else leaf(LEAVES[n - NFNS]);
    if (g_timing) g_ns[n] += now_ns() - t0;
    return true;
}

void list(FILE *f) {
    for (int n = 0; n < NFNS + NLEAVES; n++) fprintf(f, "%s\t%s\n", family_of(n), name_of(n));
}

void report(FILE *f) {
    for (int n = 0; n < NFNS + NLEAVES; n++)
        if (g_calls[n]) fprintf(f, "native_calls\t%s\t%llu\t%llu\n", name_of(n), (unsigned long long)g_calls[n], (unsigned long long)(g_ns[n] / 1000));
}

}  // namespace nat
}  // namespace emu
