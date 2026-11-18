// Guest memory, heap, loader, calls and logs of the bridge (PLAN.md DES-04, DES-05).
// Every guest address is 32-bit. Host address = g_base + guest address.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "core.h"

namespace emu {

// Fixed guest layout, the same as the oracle (universal/oracle/src/oracle.h).
constexpr uint32_t THUNK_BASE = 0x0f000000u;  // svc thunks: 8 bytes each, "svc #n; bx lr"
constexpr uint32_t THUNK_CODE = 0x2000u;      // bytes of thunk code (1024 thunks)
constexpr uint32_t DATA_BASE  = 0x0f010000u;  // fake JNI tables, FILE records, errno, struct tm
constexpr uint32_t THUNK_SIZE = 0x00100000u;
constexpr uint32_t SO_BASE    = 0x10000000u;
constexpr uint32_t HEAP_BASE  = 0x20000000u;
constexpr uint32_t HEAP_SIZE  = 0x18000000u;
constexpr uint32_t JHEAP_BASE = 0x38000000u;
constexpr uint32_t JHEAP_SIZE = 0x06000000u;
constexpr uint32_t STACK_BASE = 0x3e000000u;
constexpr uint32_t STACK_SIZE = 0x01000000u;
constexpr uint32_t VA(uint32_t a) { return SO_BASE + a; }
constexpr uint32_t GHIDRA(uint32_t a) { return SO_BASE + a - 0x10000u; }

// svc numbers
constexpr uint32_t SVC_IMPORT = 0x000;  // + import index (shims.cpp)
constexpr uint32_t SVC_ENV    = 0x100;  // + JNIEnv slot
constexpr uint32_t SVC_VM     = 0x200;  // + JavaVM slot
constexpr uint32_t SVC_RETURN = 0x300;  // return trap of a host-to-guest call
constexpr uint32_t SVC_PROF_EXIT = 0x3fe;  // prof: exit thunk of a timed function
constexpr uint32_t SVC_PROF_ENTRY = 0x400; // prof: + function index (entry hooks, no thunk code)
constexpr uint32_t SVC_NATIVE = 0x10000;   // + native replacement index (native/native.cpp)
extern void (*g_prof_svc)(uint32_t n);
inline uint32_t thunk(uint32_t n) { return THUNK_BASE + 8u * n; }

extern uint8_t *g_base;
extern Core *g_core;

// ---- memory (guest.cpp) ----
void mem_init();                               // reserve 4 GiB + guard, commit the fixed ranges
bool mem_valid(uint32_t a, uint32_t len);      // the whole range is committed
inline void *H(uint32_t a) { return g_base + a; }
void *P(uint32_t a, uint32_t len);             // checked host pointer, dies on a bad range
const char *S(uint32_t a);                     // checked guest C string, dies without NUL
inline uint32_t R32(uint32_t a) { uint32_t v; memcpy(&v, P(a, 4), 4); return v; }
inline uint16_t R16(uint32_t a) { uint16_t v; memcpy(&v, P(a, 2), 2); return v; }
inline uint8_t R8(uint32_t a) { return *(uint8_t *)P(a, 1); }
inline void W32(uint32_t a, uint32_t v) { memcpy(P(a, 4), &v, 4); }
inline void W8(uint32_t a, uint8_t v) { *(uint8_t *)P(a, 1) = v; }

// ---- heap (guest.cpp), a copy of universal/oracle/src/heap.c on guest addresses ----
struct heap_t {
    uint32_t base, top, end;
    uint32_t freel[32];
    uint32_t live, peak, allocs, frees;
    const char *name;
};
extern heap_t g_heap, g_jheap;
void heap_init(heap_t *h, uint32_t base, uint32_t size, const char *name);
uint32_t heap_alloc(heap_t *h, uint32_t n);
void heap_free(heap_t *h, uint32_t p);
int heap_size(heap_t *h, uint32_t p);

// ---- loader (loader.cpp) ----
struct so_patch_t { uint32_t off; uint16_t before, after; const char *why; };
void so_load(const char *path, const so_patch_t *patches, int npatches);
void so_load_mem(const uint8_t *data, uint32_t size, const so_patch_t *patches, int npatches);
uint32_t so_sym(const char *name);             // .dynsym then .symtab; guest address, Thumb bit kept
const char *so_name_at(uint32_t addr, uint32_t *off);
void so_run_init();
uint32_t so_text_end();
uint32_t so_end();

// ---- calls (guest.cpp) ----
// Calls a guest function with up to 12 word arguments (AAPCS). Returns r0 | r1 << 32.
uint64_t gcall(uint32_t fn, const uint32_t *args, int n);
inline uint64_t gcall0(uint32_t fn) { return gcall(fn, nullptr, 0); }
// Argument i of the current svc (r0 to r3, then the guest stack).
uint32_t arg(int i);
void ret(uint32_t r0);
void ret64(uint64_t v);
void core_init();

// ---- logs (guest.cpp) ----
extern FILE *g_log;
extern int g_phase;                            // 0 boot, 1 frames
extern uint32_t g_frame;
extern uint64_t g_now_ms;
void olog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void die(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// ---- shims (shims.cpp) ----
int shim_index(const char *name);              // import index, or -1
uint32_t shim_data(const char *name);          // guest address of a data import, or 0
void shim_svc(uint32_t idx);
void shims_init();
void shims_report(FILE *f);
extern int g_errno_addr_dummy;
void set_errno(int e);
extern char g_files_root[256], g_assets_root[256];
// App mode (the bridge): the real clock and time zone, and GL goes to g_gl_hook (gl_real.cpp).
// The runner leaves both off: the frame clock, UTC, and GL capture.
extern bool g_app;
extern void (*g_gl_hook)(uint32_t gl_index);
extern char g_files_root2[256];                // the real path of g_files_root (app mode)
extern uint8_t g_fb[512 * 512 * 2];
extern int g_fb_w, g_fb_h, g_fb_uploads;

// ---- formatter (fmt.cpp) ----
struct ArgCursor {
    int reg;          // next register index (0 to 3); 4 means the stack
    uint32_t sp;      // guest stack area of the stack arguments
    uint32_t ap;      // va_list mode: guest address of the next argument, or 0
    uint32_t word();
    uint64_t dword();
    static ArgCursor regs(int first) { ArgCursor c; c.reg = first; c.sp = 0; c.ap = 0; return c; }
    static ArgCursor va(uint32_t ap) { ArgCursor c; c.reg = 4; c.sp = 0; c.ap = ap; return c; }
};
int gformat(char *out, size_t cap, const char *fmt, ArgCursor &a);  // returns the full length, as vsnprintf

// ---- fake JNI (jni_fake.cpp) ----
uint32_t jni_vm();
uint32_t jni_env();
uint32_t jni_natives_class();
void jni_end_call();
void jni_svc_env(uint32_t slot);
void jni_svc_vm(uint32_t slot);
void jni_init();
void timers_fire(void (*cb)(int idx, int stamp));
extern int g_lang_english;
extern int g_sound_log;

}  // namespace emu
