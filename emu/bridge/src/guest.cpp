// Guest memory, heap, host-to-guest calls, svc dispatch, logs and crash reports (DES-04, DES-05).
#include <csignal>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include "guest.h"
#include "native/native.h"

namespace emu {

uint8_t *g_base;
Core *g_core;
FILE *g_log;
int g_phase;
uint32_t g_frame;
uint64_t g_now_ms;
heap_t g_heap, g_jheap;
void (*g_prof_svc)(uint32_t n);

// ---- logs ----
void olog(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    if (g_phase) fprintf(g_log, "%u\t", g_frame); else fputs("boot\t", g_log);
    vfprintf(g_log, fmt, ap); fputc('\n', g_log);
    va_end(ap);
}

static void where(FILE *f, const char *what, uint32_t a) {
    uint32_t off; const char *n = so_name_at(a, &off);
    if (n) fprintf(f, "  %s 0x%08x = ghidra 0x%x %s+0x%x\n", what, a, a - SO_BASE + 0x10000, n, off);
    else fprintf(f, "  %s 0x%08x\n", what, a);
}
static void crash_report(const char *why) {
    FILE *f = stderr;
    for (int pass = 0; pass < 2; pass++, f = g_log) {
        if (!f) break;
        fprintf(f, "RUNNER CRASH at frame %u: %s\n", g_frame, why);
        if (g_core) {
            uint32_t *r = &g_core->reg(0);
            where(f, "pc", r[15]); where(f, "lr", r[14]);
            fprintf(f, "  cpsr %08x sp %08x r0 %08x r1 %08x r2 %08x r3 %08x r4 %08x r5 %08x r6 %08x r7 %08x r12 %08x\n",
                    g_core->cpsr(), r[13], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[12]);
            uint32_t sp = r[13];
            if (sp >= STACK_BASE && sp < STACK_BASE + STACK_SIZE)
                for (int i = 0; i < 400 && sp + 4u * i < STACK_BASE + STACK_SIZE; i++) {
                    uint32_t v; memcpy(&v, g_base + sp + 4u * i, 4);
                    if (v >= SO_BASE && v < so_text_end() && (v & 1)) where(f, "stack", v);
                }
        }
        fflush(f);
    }
}
void die(const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    fprintf(stderr, "RUNNER FATAL (frame %u): %s\n", g_frame, buf);
    olog("FATAL %s", buf);
    crash_report(buf);
    if (g_log) fflush(g_log);
    _exit(3);
}

// ---- memory ----
struct range { uint32_t base, size; const char *name; };
static const range ranges[] = {
    {THUNK_BASE, THUNK_SIZE, "thunk page"}, {HEAP_BASE, HEAP_SIZE, "game heap"},
    {JHEAP_BASE, JHEAP_SIZE, "java heap"}, {STACK_BASE, STACK_SIZE, "game stack"},
};
static uint32_t so_lo, so_hi;  // committed image range, set by the loader through mem_commit_image
void mem_commit_image(uint32_t base, uint32_t size);
void mem_commit_image(uint32_t base, uint32_t size) {
    if (mprotect(g_base + base, size, PROT_READ | PROT_WRITE)) die("cannot commit the guest image");
    so_lo = base; so_hi = base + size;
}
void mem_init() {
    // 4 GiB of guest space plus a 64 KiB guard, so a 32-bit address plus a small offset never leaves the block.
    const size_t span = (1ull << 32) + 0x10000;
    void *p = mmap(nullptr, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) { fprintf(stderr, "cannot reserve 4 GiB of guest space\n"); _exit(3); }
    g_base = (uint8_t *)p;
    for (const range &r : ranges)
        if (mprotect(g_base + r.base, r.size, PROT_READ | PROT_WRITE)) die("cannot commit %s", r.name);
}
bool mem_valid(uint32_t a, uint32_t len) {
    uint64_t e = (uint64_t)a + len;
    if (a >= so_lo && e <= so_hi) return true;
    for (const range &r : ranges) if (a >= r.base && e <= (uint64_t)r.base + r.size) return true;
    return false;
}
void *P(uint32_t a, uint32_t len) {
    if (len && !mem_valid(a, len)) die("bad guest range 0x%08x + %u", a, len);
    return g_base + a;
}
const char *S(uint32_t a) {
    if (!mem_valid(a, 1)) die("bad guest string 0x%08x", a);
    const char *s = (const char *)(g_base + a);
    for (uint32_t i = 0;; i++) {
        if (!mem_valid(a + i, 1)) die("guest string 0x%08x has no end", a);
        if (!s[i]) return s;
    }
}

// ---- heap: deterministic, zero-filled, the same as the oracle allocator ----
void heap_init(heap_t *h, uint32_t base, uint32_t size, const char *name) {
    memset(h, 0, sizeof *h);
    h->base = h->top = base; h->end = base + size; h->name = name;
}
#define MAGIC 0xa1e1b10cu
uint32_t heap_alloc(heap_t *h, uint32_t n) {
    int c = 0; uint32_t sz = 16;
    while (sz < n + 8) { sz <<= 1; c++; if (c >= 31) die("%s: alloc of %u bytes too large", h->name, n); }
    uint32_t b;
    if (h->freel[c]) {
        b = h->freel[c];
        h->freel[c] = R32(b + 8);
        memset(P(b + 8, sz - 8), 0, sz - 8);
    } else {
        if (h->top + sz > h->end || h->top + sz < h->top) die("%s: out of memory (%u bytes asked, 0x%x used)", h->name, n, h->top - h->base);
        b = h->top;
        h->top += sz;
    }
    W32(b, (uint32_t)c | (n < (1u << 27) ? n << 5 : 0)); W32(b + 4, MAGIC);
    h->allocs++; h->live += sz; if (h->live > h->peak) h->peak = h->live;
    return b + 8;
}
void heap_free(heap_t *h, uint32_t p) {
    if (!p) return;
    uint32_t b = p - 8;
    if (b < h->base || b >= h->top || R32(b + 4) != MAGIC) die("%s: free of bad pointer 0x%x", h->name, p);
    int c = (int)(R32(b) & 31);
    W32(b + 4, 0);
    W32(b + 8, h->freel[c]); h->freel[c] = b;
    h->frees++; h->live -= 16u << c;
}
int heap_size(heap_t *h, uint32_t p) {
    uint32_t b = p - 8;
    if (!p || b < h->base || b >= h->top || R32(b + 4) != MAGIC) return -1;
    return (int)(R32(b) >> 5);
}

// ---- svc dispatch ----
struct Hooks final : CoreHooks {
    void svc(uint32_t n) override {
        if (n < SVC_ENV) shim_svc(n - SVC_IMPORT);
        else if (n < SVC_VM) jni_svc_env(n - SVC_ENV);
        else if (n < SVC_RETURN) jni_svc_vm(n - SVC_VM);
        else if (n == SVC_RETURN) g_core->stop();
        else if (n >= SVC_NATIVE && nat::svc(n - SVC_NATIVE)) {}
        else if (n >= SVC_PROF_EXIT && g_prof_svc) g_prof_svc(n);
        else die("unknown svc 0x%x", n);
    }
    void fault(uint32_t pc, const char *what) override { die("%s (pc 0x%08x)", what, pc); }
};
static Hooks hooks;

void core_init() {
    // Thunk code: "svc #n; bx lr" in ARM state for every n.
    for (uint32_t n = 0; n < THUNK_CODE / 8; n++) { W32(thunk(n), 0xef000000u | n); W32(thunk(n) + 4, 0xe12fff1eu); }
    g_core = Core::create_dynarmic(g_base, &hooks, mem_valid);
}

uint32_t arg(int i) {
    if (i < 4) return g_core->reg(i);
    return R32(g_core->reg(13) + 4u * (i - 4));
}
void ret(uint32_t r0) { g_core->reg(0) = r0; }
void ret64(uint64_t v) { g_core->reg(0) = (uint32_t)v; g_core->reg(1) = (uint32_t)(v >> 32); }

uint64_t gcall(uint32_t fn, const uint32_t *args, int n) {
    if (g_core->executing()) die("nested host-to-guest call to 0x%x", fn);
    uint32_t sp = STACK_BASE + STACK_SIZE - 64;
    int extra = n > 4 ? n - 4 : 0;
    sp -= 4u * extra; sp &= ~7u;
    for (int i = 0; i < extra; i++) W32(sp + 4u * i, args[4 + i]);
    for (int i = 0; i < 4; i++) g_core->reg(i) = i < n ? args[i] : 0;
    g_core->reg(13) = sp;
    g_core->reg(14) = thunk(SVC_RETURN);
    g_core->reg(15) = fn & ~1u;
    g_core->set_cpsr(0x10u | ((fn & 1u) ? 0x20u : 0u));  // user mode, Thumb bit from the address
    g_core->run();
    if (g_core->reg(15) != thunk(SVC_RETURN) + 4 && g_core->reg(15) != thunk(SVC_RETURN))
        die("guest call to 0x%x stopped at pc 0x%x", fn, g_core->reg(15));
    return g_core->reg(0) | (uint64_t)g_core->reg(1) << 32;
}

}  // namespace emu
