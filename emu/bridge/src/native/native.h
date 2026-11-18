// Native replacements of hot guest functions (PLAN.md DES-12, TASK-E4.3).
// Each replacement is a C++ port of a verified Java port in universal/engine/src/arel/engine/gfx/
// (Blit, Screen). Never a port of decompiled code (CC-7).
// A replacement runs on guest memory through the host pointer g_base. A pixel pointer is an index
// into PX() (guest address / 2), and a byte pointer is a guest address into BY().
// The code uses Java int rules: compile with -fwrapv. ushr() is the Java >>> operator.
#pragma once
#include <cstdint>
#include "../guest.h"

namespace emu {
namespace nat {

inline uint16_t *PX() { return reinterpret_cast<uint16_t *>(g_base); }
inline const uint8_t *BY() { return g_base; }
inline int ushr(int x, int n) { return (int)((uint32_t)x >> n); }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
// A guest pixel pointer as a PX() index. The guest never passes an odd one (checked).
int pix(uint32_t addr);

// The game RNG (universal/spec/random.md): one global word at VA 0x62385c.
int rng_random(int n);

// Guest data that the ports read.
constexpr uint32_t FRAME_PTR = 0x623720;  // VA: GxGetFrameT1 returns this word; screen object = it + 8
constexpr uint32_t FX_TABLE = 0x13503c;   // Ghidra: MakeFxTable output, 4096 bytes
constexpr uint32_t RNG_STATE = 0x62385c;  // VA
uint32_t screen_obj();                    // the main screen object (GcxGetMainScreenBuffer)

// ---- Blit (blit.cpp): the DrawOP leaves ----
enum Fmt { BASIC = 0, EX = 1, S16 = 2, ALPHA = 3, EX_ALPHA = 4, S16_ALPHA = 5 };
enum Op { COPY = 0, BLEND16 = 1, ADD = 2, SUB = 3, VOID = 4, LIGHTEN = 6, GRAY = 10, RGB = 11, RGBHALF = 12, RGBADD = 13,
          RGBMULTI = 14, OUTLINE = 15, ENLARGE = 16, FLIP = 17, LIGHTING = 18, FX = 19 };
int auto_format(int p);
void draw_auto(int op, bool clip, int d, int p, int pal, int pitch, int cx, int cw, int cy, int ch, int param);
void draw(int op, int fmt, bool clip, int d, int p, int pal, int pitch, int cx, int cw, int cy, int ch, int param);
void draw_enlarge(int fmt, bool clip, int d, int p, int pal, int stride, int cx, int cw, int cy, int ch, int k, int a, bool add);
void draw_outline(bool clip, int d, int p, int pal, int pitch, int cx, int cw, int cy, int ch, int c);
void draw16(int op, int d, int so, int w, int h, int ds, int ss, int param, int key, int a, bool add);
int mix(int c, int w);

// ---- Screen (screen.cpp) ----
void put(int x, int y, int w, int h, int s, int pal, int op, int param, uint32_t target);
void rgb_blend16(int d, int pitch_bytes, int w, int h, int c, int level);
void fade_out16(int d, int pitch_bytes, int w, int h, int level);
void wave_h16(int d, int pitch_bytes, int w, int h, int tab, int n);
void wave_v16(int d, int pitch_bytes, int w, int h, int tab, int n);
void rgb_blend_real(int x, int y, int w, int h, int colour, int level);
void fade_out_real(int x, int y, int w, int h, int level);
void wave_real(int x, int y, int w, int h, int tab, int n, int vertical);
// ---- Primitives (prim.cpp). g is the CGsGraphics address. base is a PX() index. ----
int pixel_from_rgb(int r, int g, int b);
void mc_fill_rect(int base, int x, int y, int w, int h, int colour);
void mc_put_pixel(int base, int x, int y, int colour);
void prim_clear_screen(uint32_t g, int c);
void prim_line_dda(uint32_t g, int x1, int y1, int x2, int y2);
void prim_draw_line(uint32_t g, int x1, int y1, int x2, int y2, int c);
void prim_draw_rect(uint32_t g, int x, int y, int w, int h, int c);
void prim_draw_fill_rect(uint32_t g, int x, int y, int w, int h, int c);
void prim_draw_fill_round_rect(uint32_t g, int x, int y, int w, int h, int r, int c);
void prim_fill_ellipse(uint32_t g, int x, int y, int w, int h, int c, int q);
// ---- Text (text.cpp): CGxBFont::DrawText on the game font. ----
void draw_text(uint32_t font, uint32_t s, int len, int x, int y, int wrap, bool skip_space, bool ks);
// Mutation check (DES-12 step 4): AREL_NATIVE_MUT=N breaks rule N (see NATIVE.md). 0: no mutation.
extern int g_mut;
#ifdef AREL_NATIVE_MUT_BUILD
#define MUT(n) (g_mut == (n))
#else
#define MUT(n) false
#endif

// ---- registry (native.cpp) ----
// Hooks every replacement that SPEC selects: "all" (default), "none", or a comma list of names
// and "-name" entries ("all,-PutCompressImg"). A name is a mangled symbol or a family name.
// Call after so_load and before the first guest call. Logs each hooked function.
// Returns the count of replacements that it turned on. total() is the count of all replacements.
int install(const char *spec);
int total();
// The svc handler for SVC_NATIVE + n. True if n is a native replacement.
bool svc(uint32_t n);
// Names of all replacements, for NATIVE.md and the runner (-native-list).
void list(FILE *f);
// Calls and time per replacement (debug and runner).
void report(FILE *f);

}  // namespace nat
}  // namespace emu
