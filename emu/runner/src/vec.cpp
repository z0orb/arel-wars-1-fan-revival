// Vector mode of the runner (PLAN.md TASK-E4.1, DES-12). A copy of universal/oracle/src/vec.c,
// changed for guest memory: blobs, the scratch buffer and the font names live in the guest heap,
// and each case is a gcall. The case format and the vec.txt lines are the same as the oracle's:
//   arel-emu -so LIB -assets DIR -files DIR -out DIR -vec CASES [-vec-dump LIST] [-peek A:N,...]
// vec.txt: "INDEX<TAB>FNV64<TAB>RNG" per case (see vec.c for the tags).
#include <cstdlib>
#include <vector>
#include "../../bridge/src/guest.h"
#include "vec.h"

namespace emu {

static constexpr uint64_t FNV0 = 1469598103934665603ull;
static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static const uint8_t *cur, *end;
static uint32_t rd(int n) {
    if (cur + n > end) die("vec: case file truncated");
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)cur[i] << (8 * i);
    cur += n;
    return v;
}

static int in_spec(const char *spec, int i) {
    if (!spec) return 0;
    for (const char *p = spec; *p;) {
        char *e; long a = strtol(p, &e, 10), b = a;
        if (*e == '-') b = strtol(e + 1, &e, 10);
        if (i >= a && i <= b) return 1;
        p = *e == ',' ? e + 1 : e;
        if (e == p && *p) p++;
    }
    return 0;
}

static constexpr uint32_t SCRATCH_PX = 2048 * 1024;
static uint32_t g_scratch;  // guest address of the scratch pixels

static uint32_t g_font;
static uint8_t g_font_save[0x84];
static uint32_t gstr(const char *s) {
    uint32_t a = heap_alloc(&g_heap, (uint32_t)strlen(s) + 1);
    memcpy(P(a, (uint32_t)strlen(s) + 1), s, strlen(s) + 1);
    return a;
}
static uint32_t game_font() {
    if (g_font) return g_font;
    static const char *nm[3] = {"font/synGung9_11.ft2", "font/synGulimAsc5_11.ft2", "font/exB9_11.ft2"};
    uint32_t p[3];
    for (int i = 0; i < 3; i++) p[i] = gstr(nm[i]);
    uint32_t slot = heap_alloc(&g_heap, 4);
    W32(slot, 0);
    uint32_t create = so_sym("_Z13GxCreateBFontPKcS0_S0_RP8CGxBFontS0_");
    uint32_t color = so_sym("_ZN8CGxBFont8SetColorEj");
    uint32_t hs = so_sym("_ZN8CGxBFont9SetHSpaceEi");
    uint32_t vs = so_sym("_ZN8CGxBFont9SetVSpaceEi");
    if (!create || !color || !hs || !vs) die("vec: font symbols not found");
    uint32_t a[5] = {p[0], p[1], p[2], slot, 0};
    int r = (int)(uint32_t)gcall(create, a, 5);
    if (r || !R32(slot)) die("vec: GxCreateBFont returned %d", r);
    g_font = R32(slot);
    uint32_t b[2] = {g_font, 0xffff}; gcall(color, b, 2);
    b[1] = 1; gcall(hs, b, 2);
    b[1] = 3; gcall(vs, b, 2);
    memcpy(g_font_save, P(g_font, sizeof g_font_save), sizeof g_font_save);
    olog("vec: game font 0x%x, clip %d %d %d %d, +0x50 %d +0x54 %d", g_font, (int16_t)R16(g_font + 0x48),
         (int16_t)R16(g_font + 0x4a), (int16_t)R16(g_font + 0x4c), (int16_t)R16(g_font + 0x4e),
         (int32_t)R32(g_font + 0x50), (int32_t)R32(g_font + 0x54));
    return g_font;
}

void vec_peek(const char *spec, const char *out_dir) {
    char path[400]; snprintf(path, sizeof path, "%s/peek.txt", out_dir);
    FILE *o = fopen(path, "w");
    if (!o) die("vec: cannot write %s", path);
    for (const char *p = spec; *p;) {
        char *e; uint32_t a = (uint32_t)strtoul(p, &e, 16); int n = 1;
        if (*e == ':') n = (int)strtol(e + 1, &e, 10);
        for (int i = 0; i < n; i++) {
            uint32_t w = R32(GHIDRA(a + 4u * (uint32_t)i)), off = 0;
            const char *s = w >= SO_BASE && w < so_end() ? so_name_at(w & ~1u, &off) : nullptr;
            fprintf(o, "%x\t%08x\t%s+0x%x\n", a + 4u * (uint32_t)i, w, s ? s : "-", off);
        }
        p = *e == ',' ? e + 1 : e;
        if (e == p && *p) p++;
    }
    fclose(o);
}

void vec_run(const char *cases, const char *dump_spec, const char *out_dir) {
    FILE *f = fopen(cases, "rb");
    if (!f) die("vec: cannot open %s", cases);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)n);
    if (fread(buf.data(), 1, (size_t)n, f) != (size_t)n) die("vec: cannot read %s", cases);
    fclose(f);
    cur = buf.data(); end = buf.data() + n;
    if (rd(4) != 0x43455641u || rd(4) != 1) die("vec: bad header in %s", cases);
    // The blobs go into one guest block, each at a multiple of 4, as in the file.
    uint32_t nblob = rd(4);
    const uint8_t *bstart = cur;
    std::vector<uint32_t> boff(nblob ? nblob : 1);
    for (uint32_t i = 0; i < nblob; i++) {
        uint32_t len = rd(4);
        boff[i] = (uint32_t)(cur - bstart);
        cur += (len + 3) & ~3u;
        if (cur > end) die("vec: blob %u truncated", i);
    }
    uint32_t bsize = (uint32_t)(cur - bstart);
    uint32_t gblob = heap_alloc(&g_heap, bsize ? bsize : 4);
    if (bsize) memcpy(P(gblob, bsize), bstart, bsize);
    std::vector<uint32_t> blob(nblob ? nblob : 1);
    for (uint32_t i = 0; i < nblob; i++) blob[i] = gblob + boff[i];
    if (!g_scratch) g_scratch = heap_alloc(&g_heap, SCRATCH_PX * 2);

    uint32_t scr = (uint32_t)gcall0(VA(0xd57b4) | 1u);
    uint32_t surf = R32(scr);
    int sw0 = (int32_t)R32(surf), sbpp = (int32_t)R32(surf + 0xc);
    uint32_t spx = R32(surf + 0x10);
    uint32_t clip = scr + 0x48;
    olog("vec: screen object 0x%x surface 0x%x width %d bpp %d pixels 0x%x clip %d %d %d %d yoff %d", scr, surf, sw0, sbpp,
         spx, (int16_t)R16(clip), (int16_t)R16(clip + 2), (int16_t)R16(clip + 4), (int16_t)R16(clip + 6), (int32_t)R32(scr + 0x44));
    if (sbpp != 16 || sw0 != 480) die("vec: unexpected screen %dx? bpp %d", sw0, sbpp);

    uint32_t ncase = rd(4);
    char path[400]; snprintf(path, sizeof path, "%s/vec.txt", out_dir);
    FILE *o = fopen(path, "w");
    if (!o) die("vec: cannot write %s", path);
    char name[256]; uint32_t last_fn = 0;
    for (uint32_t c = 0; c < ncase; c++) {
        g_frame = c;  // crash reports name the case
        int nl = (int)rd(1);
        memcpy(name, cur, (size_t)nl); name[nl] = 0; cur += nl;
        int target = (int)rd(1), w = (int)rd(2), h = (int)rd(2);
        uint32_t seed = rd(4);
        int hasclip = (int)rd(1);
        int16_t cl[4] = {0};
        if (hasclip) for (int k = 0; k < 4; k++) cl[k] = (int16_t)rd(2);
        int na = (int)rd(1);
        if (na > 32) die("vec: case %u has %d argument entries", c, na);
        uint32_t px;
        if (target == 1) { px = spx; w = 480; h = 320; }
        else { px = g_scratch; if ((long)w * h > (long)SCRATCH_PX) die("vec: scratch %dx%d too big", w, h); }
        uint32_t a[12] = {0}, poff[4], pval[4], psave[4], foff[8], fval[8];
        uint32_t laddr[8], lval[8], lsave[8];
        int np = 0, nk = 0, nf = 0, nl2 = 0;
        for (int k = 0; k < na; k++) {
            int tag = (int)rd(1); uint32_t v = rd(4);
            if (tag == 10) {
                if (nk == 12) die("vec: case %u has more than 12 arguments", c);
                a[nk++] = GHIDRA(v);
                continue;
            }
            if (tag == 11) {
                if (nl2 == 8 || k + 1 >= na || (v & 3) || GHIDRA(v) < SO_BASE || GHIDRA(v) >= so_end()) die("vec: case %u bad library poke", c);
                rd(1); laddr[nl2] = GHIDRA(v); lval[nl2] = rd(4); nl2++; k++;
                continue;
            }
            if (tag == 6 || tag == 7) {
                if (nf == 8 || k + 1 >= na || v >= 0x84 || (v & 3)) die("vec: case %u bad font poke", c);
                rd(1); uint32_t val = rd(4); k++;
                if (tag == 7 && val >= nblob) die("vec: case %u bad font fixup", c);
                game_font(); foff[nf] = v; fval[nf] = tag == 7 ? blob[val] : val; nf++;
                continue;
            }
            if (tag == 9) {
                if (k + 1 >= na) die("vec: case %u bad fixup", c);
                rd(1); uint32_t bo = rd(4), bb = bo >> 8, oo = bo & 0xff; k++;
                if (bb >= nblob || (oo & 3)) die("vec: case %u bad fixup", c);
                W32(blob[bb] + oo, GHIDRA(v));
                continue;
            }
            if (tag == 8) {
                if (nk == 12) die("vec: case %u has more than 12 arguments", c);
                a[nk++] = game_font();
                continue;
            }
            if (tag == 3) {
                if (np == 4 || k + 1 >= na || v > 0x60 || (v & 3)) die("vec: case %u bad poke", c);
                rd(1); poff[np] = v; pval[np] = rd(4); np++; k++;
                continue;
            }
            if (tag == 4 || tag == 5) {
                if (k + 1 >= na) die("vec: case %u bad fixup", c);
                rd(1); uint32_t bo = rd(4), bb = bo >> 8, oo = bo & 0xff; k++;
                if (bb >= nblob || (oo & 3) || (tag == 4 && v >= nblob)) die("vec: case %u bad fixup", c);
                W32(blob[bb] + oo, tag == 4 ? blob[v] : px + 2u * (uint32_t)(int32_t)v);
                continue;
            }
            if (nk == 12) die("vec: case %u has more than 12 arguments", c);
            int kk = nk++;
            if (tag == 0) a[kk] = v;
            else if (tag == 1) { if (v >= nblob) die("vec: case %u blob %u", c, v); a[kk] = blob[v]; }
            else if (tag == 2) a[kk] = px + 2u * (uint32_t)(int32_t)v;
            else die("vec: case %u tag %d", c, tag);
        }
        uint16_t *hp = (uint16_t *)P(px, (uint32_t)(w * h * 2));
        uint32_t x = seed;
        for (long i = 0; i < (long)w * h; i++) {
            if (seed) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; hp[i] = (uint16_t)x; }
            else hp[i] = 0;
        }
        uint8_t saved[8]; memcpy(saved, P(clip, 8), 8);
        if (hasclip) memcpy(P(clip, 8), cl, 8);
        for (int k = 0; k < np; k++) { psave[k] = R32(scr + poff[k]); W32(scr + poff[k], pval[k]); }
        for (int k = 0; k < nf; k++) W32(g_font + foff[k], fval[k]);
        for (int k = 0; k < nl2; k++) { lsave[k] = R32(laddr[k]); W32(laddr[k], lval[k]); }
        uint32_t fn = !strncmp(name, "0x", 2) ? (GHIDRA((uint32_t)strtoul(name, nullptr, 16)) | 1u) : so_sym(name);
        if (!fn) die("vec: function %s not found", name);
        if (fn != last_fn) { olog("vec: case %u calls %s at 0x%x", c, name, fn); last_fn = fn; }
        W32(VA(0x62385c), seed);  // the game RNG state (spec/random.md)
        gcall(fn, a, 12);
        memcpy(P(clip, 8), saved, 8);
        for (int k = np - 1; k >= 0; k--) W32(scr + poff[k], psave[k]);
        for (int k = nl2 - 1; k >= 0; k--) W32(laddr[k], lsave[k]);
        if (g_font) memcpy(P(g_font, sizeof g_font_save), g_font_save, sizeof g_font_save);
        fprintf(o, "%u\t%016llx\t%08x\n", c, (unsigned long long)fnv(FNV0, hp, (size_t)w * h * 2), R32(VA(0x62385c)));
        if (in_spec(dump_spec, (int)c)) {
            snprintf(path, sizeof path, "%s/vec-%u.rgb565", out_dir, c);
            FILE *d = fopen(path, "wb");
            uint16_t hd[2] = {(uint16_t)w, (uint16_t)h};
            fwrite(hd, 2, 2, d); fwrite(hp, 2, (size_t)w * h, d); fclose(d);
        }
        if (c % 256 == 255) fflush(o);
    }
    fclose(o);
    heap_free(&g_heap, gblob);
    olog("vec: %u cases done", ncase);
}

}  // namespace emu
