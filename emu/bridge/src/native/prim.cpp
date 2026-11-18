// The primitives: a C++ port of universal/engine/src/arel/engine/gfx/Prim.java (spec/render.md,
// "Primitives"). MC_grpFillRect, MC_grpPutPixel and the CGsGraphics methods ClearScreen,
// LineDDAFixedPoint, DrawLine, DrawRect, DrawFillRect, DrawFillRoundRect and DrawNativeFillEllipse.
// The state that Prim.java keeps in fields is read from guest memory:
//   CGsGraphics (this): +0x8 screen object, +0xc MC context, +0x38 mode, +0x3c extra rows,
//     byte +0x44 additive lines, +0x48 clear width, +0x4c clear height.
//   MC context: key 1 (foreground colour) +0x14, key 4 (alpha) +0x20, key 5 +0x2c.
//   Screen object: word 0 the surface (+0x10 pixels), +0x44 y offset, s16 clip rect at +0x48.
#include "native.h"

namespace emu {
namespace nat {

static constexpr int W = 480, H = 320;

// Java int division. -fwrapv does not cover INT_MIN / -1, which traps on x86.
static inline int jdiv(int a, int b) { return b == -1 ? -a : a / b; }

int pixel_from_rgb(int r, int g, int b) { return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3); }

// The MC calls' colour: above 0xffff (signed) it is 0xRRGGBB, else the pixel itself.
static int mc_colour(int c) {
    if (c <= 0xffff) return c & 0xffff;
    return ushr(c << 8, 27) << 11 | ushr(c << 16, 26) << 5 | ushr(c << 24, 27);
}

// The surface of a screen object as a PX() index. Only 480 x 16-bit surfaces are ported.
static int surface_px(uint32_t obj) {
    uint32_t surf = R32(obj);
    if ((int32_t)R32(surf) != W || R32(surf + 0xc) != 16)
        die("native: prim surface is %d wide with %u bits per pixel (only 480 and 16 are ported)", (int32_t)R32(surf), R32(surf + 0xc));
    return pix(R32(surf + 0x10));
}

void mc_fill_rect(int base, int x, int y, int w, int h, int colour) {
    if (w <= 0 || h <= 0) return;
    if (x >= 0) {
        if (x >= W) return;
        if (w + x > W) w = W - x;
    } else { w += x; x = 0; }
    if (y >= 0) {
        if (y >= H) return;
        if (h + y > H) h = H - y;
    } else { h += MUT(21) ? 0 : y; y = 0; }
    uint16_t c = (uint16_t)mc_colour(colour), *t = PX();
    int p = base + y * W + x;
    for (int r = 0; r < h; r++, p += W)
        for (int j = 0; j < w; j++) t[p + j] = c;
}

void mc_put_pixel(int base, int x, int y, int colour) {
    if (x < 0 || x >= (MUT(22) ? W + 1 : W) || y < 0 || y >= H) return;
    PX()[base + y * W + x] = (uint16_t)mc_colour(colour);
}

namespace {

// One CGsGraphics call. Reads the state at the start; writes keys 1, 4, 5 and the clip rect back
// at once (the guest writes them at once too, and no other code runs during the call).
struct Gfx {
    uint32_t self, obj, ctx;
    int mode, extra_h, width, height, y_off, px;
    bool additive;
    int clip_x() const { return (int16_t)R16(obj + 0x48); }
    int clip_y() const { return (int16_t)R16(obj + 0x4a); }
    int clip_w() const { return (int16_t)R16(obj + 0x4c); }
    int clip_h() const { return (int16_t)R16(obj + 0x4e); }
    void set_clip_w(int v) { uint16_t s = (uint16_t)v; memcpy(P(obj + 0x4c, 2), &s, 2); }
    void set_clip_h(int v) { uint16_t s = (uint16_t)v; memcpy(P(obj + 0x4e, 2), &s, 2); }
    int fg() const { return (int32_t)R32(ctx + 0x14); }
    void set_fg(int v) { W32(ctx + 0x14, (uint32_t)v); }
    int weight() const { return (int32_t)R32(ctx + 0x20); }
    void set_weight(int v) { W32(ctx + 0x20, (uint32_t)v); }
    void set_ctx5(int v) { W32(ctx + 0x2c, (uint32_t)v); }

    explicit Gfx(uint32_t g) : self(g) {
        obj = R32(g + 8);
        ctx = R32(g + 0xc);
        mode = (int32_t)R32(g + 0x38);
        extra_h = (int32_t)R32(g + 0x3c);
        additive = R8(g + 0x44) != 0;
        width = (int32_t)R32(g + 0x48);
        height = (int32_t)R32(g + 0x4c);
        y_off = (int32_t)R32(obj + 0x44);
        px = surface_px(obj);
    }

    // CGsGraphics::SetForegroundColor: above 0xffff (unsigned) it is 0x??RRGGBB.
    void set_foreground(int c) {
        set_fg((uint32_t)c > 0xffffu ? pixel_from_rgb(ushr(c << 8, 24), ushr(c << 16, 24), c & 0xff) : c);
    }
    // The set-up of every Draw*: the colour's top byte becomes the alpha unless it is 0 or 0xff.
    void begin_alpha(int c) {
        int a = ushr(c, 24);
        if (mode != 2 && a != 0xff && (a != 0 || MUT(23))) set_weight(a);
    }
    // CGsGraphics::EndAlpha.
    void end_alpha() {
        if (mode == 2) return;
        set_weight(0xff);
        set_ctx5(0);
    }

    void clear_screen(int c) {
        if ((uint32_t)(c - 1) <= 0xfffdu) {
            set_fg(c);
            mc_fill_rect(px, 0, y_off, width, extra_h + height, c);
            return;
        }
        uint16_t v = (uint16_t)(c != 0 ? 0xffff : 0), *t = PX();
        int p = px + y_off * W, n = (extra_h + height) * W;
        for (int i = 0; i < n; i++) t[p + i] = v;
    }

    int lx1, ly1, lx2, ly2;

    // CGsGraphics::ClippingLine_LiangBarsky against the clip rect, into lx1..ly2.
    bool clip_line(int x1, int y1, int x2, int y2) {
        int dx = x2 - x1, dy = y2 - y1;
        if (dy == 0 && dx == 0) return false;
        int cx = clip_x(), cy = clip_y();
        int xmin = cx, xmax = cx + clip_w() - 1, ymin = cy, ymax = cy + clip_h() - (MUT(24) ? 0 : 1);
        if (dx == 0) {
            if (x1 < xmin || x1 > xmax) return false;
            if (y1 < ymin && y2 < ymin) return false;
            if (y1 > ymax && y2 > ymax) return false;
            if (y2 > y1) { y1 = imax(y1, ymin); y2 = imin(y2, ymax); }
            else { y1 = imin(y1, ymax); y2 = imax(y2, ymin); }
        } else if (dy == 0) {
            if (y1 < ymin || y1 > ymax) return false;
            if (x1 < xmin && x2 < xmin) return false;
            if (x1 > xmax && x2 > xmax) return false;
            if (x2 > x1) { x1 = imax(x1, xmin); x2 = imin(x2, xmax); }
            else { x1 = imin(x1, xmax); x2 = imax(x2, xmin); }
        } else {
            int t0 = 0, t1 = 0x10000;
            for (int k = 0; k < 4; k++) {
                int p, q;
                switch (k) {
                case 0: p = -dx; q = x1 - xmin; break;
                case 1: p = dx; q = xmax - x1; break;
                case 2: p = -dy; q = y1 - ymin; break;
                default: p = dy; q = ymax - y1; break;
                }
                int r = jdiv(q << 16, p);
                if (p < 0) {
                    if (r > t1) return false;
                    if (r > t0) t0 = r;
                } else {
                    if (t0 > r) return false;
                    if (r < t1) t1 = r;
                }
            }
            int ox = x1, oy = y1;
            int rnd = MUT(25) ? 0 : 0x8000;
            x1 = ((dx * t0 + rnd) >> 16) + ox;
            y1 = ((dy * t0 + rnd) >> 16) + oy;
            x2 = ((dx * t1 + rnd) >> 16) + ox;
            y2 = ((dy * t1 + rnd) >> 16) + oy;
        }
        lx1 = x1; ly1 = y1; lx2 = x2; ly2 = y2;
        return true;
    }

    // One line pixel at t[p] in the current mode.
    void line_pixel(uint16_t *tp, int p, int c, int a, int inv) const {
        int t = tp[p];
        if (additive) {
            int r = (t >> 11) + ushr(ushr(c, 11) * a, 8), b = (t & 31) + ushr((c & 31) * a, 8), g = (t >> 5 & 63) + ushr(ushr(c << 21, 26) * a, 8);
            if (r > 31) r = 31;
            if (b > 31) b = 31;
            if (g > (MUT(26) ? 31 : 63)) g = MUT(26) ? 31 : 63;
            tp[p] = (uint16_t)(r << 11 | b | g << 5);
        } else if (a != 0) {
            tp[p] = (uint16_t)(ushr((t >> 11) * inv + ushr(c, 11) * a, 8) << 11 | ushr((t & 31) * inv + (c & 31) * a, 8)
                               | ushr((t >> 5 & 63) * inv + ushr(c << 21, 26) * a, 8) << 5);
        } else tp[p] = (uint16_t)c;
    }

    // CGsGraphics::LineDDAFixedPoint(x1, y1, x2, y2).
    void line_dda(int x1, int y1, int x2, int y2) {
        if (!clip_line(x1, y1, x2, y2)) return;
        x1 = lx1; y1 = ly1; x2 = lx2; y2 = ly2;
        uint16_t *tp = PX();
        int p = px + (y1 + y_off) * W + x1;
        int sx, dx, sy, dy;
        if (x2 >= x1) { sx = 1; dx = x2 - x1; } else { sx = -1; dx = x1 - x2; }
        if (y2 >= y1) { sy = W; dy = y2 - y1; } else { sy = -W; dy = y1 - y2; }
        int c = fg(), a = weight() & 0xff, inv = ~a & 0xff;
        if (dx >= dy) {
            int slope = jdiv(dy << 16, dx > 0 ? dx : 1), acc = (y1 << 16) - (MUT(27) ? 0x8000 : 0x7fff), minor = y1;
            for (int n = dx + 1; n > 0; n--) {
                if (minor << 16 < acc) { minor++; p += sy; }
                line_pixel(tp, p, c, a, inv);
                acc += slope;
                if (n > 1) p += sx;
            }
        } else {
            int slope = jdiv(dx << 16, dy > 0 ? dy : 1), acc = (x1 << 16) - 0x7fff, minor = x1;
            for (int n = dy + 1; n > 0; n--) {
                if (minor << 16 < acc) { minor++; p += sx; }
                line_pixel(tp, p, c, a, inv);
                acc += slope;
                if (n > 1) p += sy;
            }
        }
    }

    void draw_line(int x1, int y1, int x2, int y2, int c) {
        begin_alpha(c);
        set_foreground(c);
        line_dda(x1, y1, x2, y2);
        end_alpha();
    }

    void draw_rect(int x, int y, int w, int h, int c) {
        begin_alpha(c);
        set_foreground(c);
        int x2 = x + w, y2 = y + h;
        line_dda(x, y, x2, y);
        line_dda(x, y2, x2, y2);
        line_dda(x, y + 1, x, y2 - 1);
        line_dda(x2, y + 1, x2, y2 - (MUT(28) ? 0 : 1));
        end_alpha();
    }

    // The fill under DrawFillRect and DrawFillRoundRect: the global fill pointer (GOT literal
    // 0x30c) at level alpha >> 4. The port runs only if that pointer holds Screen_RGBBLEND16_Real.
    void fill(int x, int y, int w, int h) {
        uint32_t fp = R32(R32(VA(0x120d68 + 0x30c)));
        if (!fp) return;
        static uint32_t want;
        if (!want) want = so_sym("_Z22Screen_RGBBLEND16_Realiiiiji");
        if (fp != want) die("native: the fill pointer is 0x%x, not Screen_RGBBLEND16_Real 0x%x", fp, want);
        rgb_blend_real(x, y, w, h, fg(), weight() >> (MUT(29) ? 3 : 4));
    }

    void draw_fill_rect(int x, int y, int w, int h, int c) {
        begin_alpha(c);
        set_foreground(c);
        fill(x, y, w, h);
        end_alpha();
    }

    void draw_fill_round_rect(int x, int y, int w, int h, int r, int c) {
        begin_alpha(c);
        set_foreground(c);
        if (r <= 7) {
            int k = r <= (MUT(30) ? 2 : 3) ? 1 : 2;
            fill(x + 1, y + 1, w - 1, h - 1);
            line_dda(x + k, y, x + w - k, y);
            line_dda(x + k, y + h, x + w - k, y + h);
            line_dda(x, y + k, x, y + h - k);
            line_dda(x + w, y + k, x + w, y + h - k);
        } else {
            fill_ellipse(x, y, r, r, c, 1);
            fill_ellipse(x, y + h - r, r, h, c, 2);
            fill_ellipse(x + w - r, y + h - r, w, h, c, 4);
            fill_ellipse(x + w - r, y, w, r, c, 8);
            int hr = (r + 1) >> 1;
            fill(x + hr, y, w - r, hr);
            fill(x + hr, y + h - hr, w - r, hr);
            fill(x, y + hr, w, h - r);
        }
        end_alpha();
    }

    static void ellipse_pixel(uint16_t *tp, int p, int ra, int ga, int ba, int inv) {
        int t = tp[p];
        tp[p] = (uint16_t)(ushr((t >> 11) * inv + ra, 8) << 11 | ushr((t >> 5 & 63) * inv + ga, 8) << 5 | ushr((t & 31) * inv + ba, 8));
    }

    // CGsGraphics::DrawNativeFillEllipse(x, y, w, h, colour, quarters). GsUnionRect leaves the
    // clip rect's w and h changed to its right and bottom edges.
    void fill_ellipse(int x, int y, int w, int h, int colour, int q) {
        if (x > x + w) x = x + w;
        if (y > y + h) y = y + h;
        int ex = x, ey = y;
        int a = w >> 1, aa = a * a, b = h >> 1, bb = b * b, cx = ex + a, aabb = aa * bb;
        // GsUnionRect(x, y, w, h, clip)
        w += x;
        h += y;
        int clx = clip_x(), cly = clip_y();
        set_clip_w(clx + clip_w());
        set_clip_h(cly + clip_h());
        int cw = clip_w(), ch = clip_h();
        if (x < clx) x = clx;
        if (y < cly) y = cly;
        if (w > cw) w = cw;
        w -= x;
        if (h > ch) h = ch;
        h -= y;
        if (w < 0 || h < 0) return;
        uint16_t *tp = PX();
        int base = px + y_off * W, c = colour, ia = 0xff, inv = 0;
        if ((uint32_t)colour > 0xffffu) {
            ia = ushr(colour, 24);
            inv = ~ia & 0xff;
            c = pixel_from_rgb(ushr(colour << 8, 24), ushr(colour << 16, 24), colour & 0xff);
        }
        int cy = ey + b;
        if (cy < y) return;
        int ra = ushr(c, 11) * ia, ga = ushr(c << 21, 26) * ia, ba = (c & 31) * ia;
        int mrow = 2 * cy - y, dy = y - cy, top = y - 1;
        for (int row = y; row <= cy; row++) {
            if (cx >= x) {
                int e0 = dy * dy * aa - aabb;
                int dx = x - cx, col = x, lcol = x - 1, mcol = 2 * cx - x;
                while (true) {
                    if (dx * dx * bb + e0 <= 0) {
                        bool lin = x <= lcol && lcol < x + w, min = x <= mcol && mcol < x + w;
                        bool tin = y <= top && top < y + h, bin = y <= mrow && mrow < y + h;
                        if ((q & 1) && lin && tin) ellipse_pixel(tp, base + top * W + lcol, ra, ga, ba, inv);
                        if ((q & (MUT(31) ? 4 : 8)) && min && tin) ellipse_pixel(tp, base + top * W + mcol, ra, ga, ba, inv);
                        if ((q & 2) && lin && bin) ellipse_pixel(tp, base + mrow * W + lcol, ra, ga, ba, inv);
                        if ((q & 4) && min && bin) ellipse_pixel(tp, base + mrow * W + mcol, ra, ga, ba, inv);
                    }
                    lcol = col;
                    col++;
                    dx++;
                    mcol--;
                    if (cx < col) break;
                }
            }
            mrow--;
            top = row;
            dy++;
        }
    }
};

}  // namespace

void prim_clear_screen(uint32_t g, int c) { Gfx(g).clear_screen(c); }
void prim_line_dda(uint32_t g, int x1, int y1, int x2, int y2) { Gfx(g).line_dda(x1, y1, x2, y2); }
void prim_draw_line(uint32_t g, int x1, int y1, int x2, int y2, int c) { Gfx(g).draw_line(x1, y1, x2, y2, c); }
void prim_draw_rect(uint32_t g, int x, int y, int w, int h, int c) { Gfx(g).draw_rect(x, y, w, h, c); }
void prim_draw_fill_rect(uint32_t g, int x, int y, int w, int h, int c) { Gfx(g).draw_fill_rect(x, y, w, h, c); }
void prim_draw_fill_round_rect(uint32_t g, int x, int y, int w, int h, int r, int c) { Gfx(g).draw_fill_round_rect(x, y, w, h, r, c); }
void prim_fill_ellipse(uint32_t g, int x, int y, int w, int h, int c, int q) { Gfx(g).fill_ellipse(x, y, w, h, c, q); }

}  // namespace nat
}  // namespace emu
