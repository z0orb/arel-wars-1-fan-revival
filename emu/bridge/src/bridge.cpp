// The bridge library (PLAN.md DES-03, DES-06, DES-08): JNI_OnLoad, the bridge lock and the log.
// JNI_OnLoad maps the embedded guest into guest memory, checks its SHA-256, runs .init_array and
// calls the guest JNI_OnLoad with the guest JavaVM. Each later entry from Java is a generated
// trampoline (tools/gen-exports.mjs) that uses Scope.
#include <android/log.h>
#include <mutex>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include "bridge.h"
#include "native/native.h"
#include "guest_hash.h"  // generated: GUEST_SHA256, the hash that params.json holds

extern "C" const uint8_t arel_guest_start[], arel_guest_end[];  // generated blob.S (.incbin)

namespace emu {

#ifndef AREL_DEBUG
#define AREL_DEBUG 0
#endif

// ---- log: one logcat line (tag ArelEmu) for each line of g_log ----
static char line_buf[2048];
static size_t line_len;
static int log_write(void *, const char *p, int n) {
    for (int i = 0; i < n; i++) {
        char c = p[i];
        if (c == '\n' || line_len == sizeof line_buf - 1) {
            line_buf[line_len] = 0;
            __android_log_write(ANDROID_LOG_INFO, "ArelEmu", line_buf);
            line_len = 0;
            if (c == '\n') continue;
        }
        line_buf[line_len++] = c;
    }
    return n;
}
static void log_open() {
    g_log = funopen(nullptr, nullptr, log_write, nullptr, nullptr);
    if (g_log) setvbuf(g_log, nullptr, _IONBF, 0);
    if (stderr) { /* crash reports also go to stderr: the same sink */ }
}

// ---- SHA-256 (FIPS 180-4), for the guest check of DES-03 ----
static void sha256(const uint8_t *m, size_t len, char out[65]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    size_t total = ((len + 9 + 63) / 64) * 64;
    for (size_t off = 0; off < total; off += 64) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            size_t p = off + (size_t)i;
            uint8_t b = p < len ? m[p] : p == len ? 0x80 : 0;
            if (p >= total - 8) b = (uint8_t)(((uint64_t)len * 8) >> (8 * (total - 1 - p)));
            blk[i] = b;
        }
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 | (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
}

// ---- debug log (TASK-E2.2) ----
bool g_dbg = AREL_DEBUG;
static std::string dbg_pending;
static FILE *dbg_file;
static std::mutex dbg_mx;  // the re-entry line can come from a thread that does not hold the bridge lock
void dbg(const char *fmt, ...) {
    if (!g_dbg) return;
    char b[1024];
    int n = snprintf(b, sizeof b, "%u\t", g_frame);
    va_list ap; va_start(ap, fmt); vsnprintf(b + n, sizeof b - (size_t)n - 1, fmt, ap); va_end(ap);
    std::lock_guard<std::mutex> g(dbg_mx);
    if (dbg_file) { fputs(b, dbg_file); fputc('\n', dbg_file); }
    else { dbg_pending += b; dbg_pending += '\n'; }
}
void dbg_set_dir(const char *files_root) {
    if (!g_dbg || dbg_file) return;
    std::string p = files_root;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t s = p.rfind('/');
    if (s == std::string::npos || s == 0) return;
    p = p.substr(0, s) + "/arelemu-debug.log";
    std::lock_guard<std::mutex> g(dbg_mx);
    dbg_file = fopen(p.c_str(), "w");
    if (!dbg_file) { olog("WARN cannot write %s", p.c_str()); return; }
    fputs(dbg_pending.c_str(), dbg_file);
    dbg_pending.clear();
    fflush(dbg_file);
    olog("debug log %s", p.c_str());
}
void dbg_flush() {
    if (!g_dbg) return;
    std::lock_guard<std::mutex> g(dbg_mx);
    if (dbg_file) fflush(dbg_file);
}
// The probe fields of the runner (runner/src/main.cpp write_probe): CPdSharing state, CInventory Gold,
// Cash and stage (universal/spec/memory-probes.md), on change, and an "alive" line every 128 frames.
// Read only: never write these words.
void dbg_probe() {
    if (!g_dbg) return;
    static uint32_t a_sh, a_inv;
    static bool have;
    static int32_t last[4];
    if (!a_sh) { char n[128]; snprintf(n, sizeof n, "_ZN12CGsSingletonI10CPdSharingE13ms_pSingletonE"); a_sh = so_sym(n); }
    if (!a_inv) { char n[128]; snprintf(n, sizeof n, "_ZN12CGsSingletonI10CInventoryE13ms_pSingletonE"); a_inv = so_sym(n); }
    uint32_t sh = a_sh && mem_valid(a_sh, 4) ? R32(a_sh) : 0, inv = a_inv && mem_valid(a_inv, 4) ? R32(a_inv) : 0;
    int32_t v[4] = {sh && mem_valid(sh, 0x100) ? (int32_t)R32(sh + 8) : -1, -1, -1, -1};
    if (inv && mem_valid(inv, 0x700)) { v[1] = (int32_t)R32(inv + 0x320); v[2] = (int32_t)R32(inv + 0x328); v[3] = (int32_t)R32(inv + 0x330); }
    if (g_frame % 128 == 0) dbg("alive");  // frames advance (the lock cycles of GC-E2.4)
    if (have && !memcmp(v, last, sizeof v)) return;
    have = true; memcpy(last, v, sizeof v);
    dbg("probe state %d gold %d cash %d stage %d", v[0], v[1], v[2], v[3]);
}

// ---- the bridge lock (DES-08) ----
static std::mutex g_lock;
static thread_local bool t_in_guest;
static void enter(const char *name) {
    if (t_in_guest) { dbg("re-entry %s", name); dbg_flush(); die("re-entry: %s was called from inside a guest call on the same thread", name); }
    g_lock.lock();
    t_in_guest = true;
}
static void leave() { t_in_guest = false; g_lock.unlock(); }

Scope::Scope(JNIEnv *env, uint32_t *fn, const char *name, bool render) : fn_(fn), name_(name), render_(render) {
    enter(name);
    prev_ = g_env; g_env = env;
    if (!*fn_) { *fn_ = so_sym(name); if (!*fn_) die("guest export %s not found", name); }
}
Scope::~Scope() {
    jni_end_call();
    if (render_) dbg_probe();
    dbg_flush();
    g_env = prev_;
    leave();
}
uint32_t Scope::env() const { return jni_env(); }
uint64_t Scope::call(const uint32_t *args, int n) {
    if (g_dbg && !render_) {
        char b[256]; int k = 0;
        for (int i = 2; i < n && k < 200; i++) k += snprintf(b + k, sizeof b - (size_t)k, " %d", (int32_t)args[i]);
        b[k] = 0;
        const char *s = strncmp(name_, "Java_com_gamevil_nexus2_Natives_", 32) ? name_ : name_ + 32;
        int tid = gettid();
        dbg("entry %s%s thread %d%s", s, b, tid, tid == getpid() ? " ui" : "");
    }
    uint64_t r = gcall(*fn_, args, n);
    if (render_) g_frame++;
    return r;
}
jobject Scope::ret_obj(uint32_t h) { jobject o = jni_host(h); return o ? g_env->NewLocalRef(o) : nullptr; }

}  // namespace emu

using namespace emu;

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_4) != JNI_OK) return JNI_ERR;
    log_open();
    g_app = true;
    uint32_t size = (uint32_t)(arel_guest_end - arel_guest_start);
    char h[65]; sha256(arel_guest_start, size, h);
    if (strcmp(h, GUEST_SHA256)) {
        __android_log_print(ANDROID_LOG_FATAL, "ArelEmu", "guest SHA-256 %s is not %s: stop", h, GUEST_SHA256);
        _exit(3);
    }
    enter("JNI_OnLoad");
    g_env = env;
    mem_init();
    // REQ-07, GC-E6.5: name the memory mode. dynarmic is built without its W^X option, so its code
    // cache needs RWX pages. The same test as the E0 probe (probe/jni/probe.cpp).
    {
        void *p = mmap(nullptr, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const char *m = p == MAP_FAILED ? "RWX denied" : "RWX";
        if (p != MAP_FAILED) munmap(p, 1 << 16);
        olog("memory mode: %s", m);
    }
    core_init();
    shims_init();
    jni_init();
    gl_real_init(AREL_DEBUG);
    heap_init(&g_heap, HEAP_BASE, HEAP_SIZE, "game heap");
    heap_init(&g_jheap, JHEAP_BASE, JHEAP_SIZE, "java heap");
    olog("bridge %s build, guest %u bytes, SHA-256 %s", AREL_DEBUG ? "debug" : "release", size, h);
    so_load_mem(arel_guest_start, size, nullptr, 0);  // the app keeps GsSleep (DES-05)
    olog("loaded the guest, text end 0x%x, image end 0x%x", so_text_end(), so_end());
    // Native replacements (DES-12): all on. A debug build takes the spec from the system property
    // debug.arelemu.native ("none", "all,-leaves", ...), so each one can be turned off alone.
    static char spec[PROP_VALUE_MAX] = "all";
    if (AREL_DEBUG) {
        char p[PROP_VALUE_MAX] = "";
        if (__system_property_get("debug.arelemu.native", p) > 0) snprintf(spec, sizeof spec, "%s", p);
    }
    int on = nat::install(spec);
    dbg("native %d of %d replacements on", on, nat::total());
    so_run_init();
    uint32_t a[2] = {jni_vm(), 0};
    uint32_t v = (uint32_t)gcall(so_sym("JNI_OnLoad"), a, 2);
    jni_end_call();
    olog("guest JNI_OnLoad -> 0x%x", v);
    g_phase = 1;
    g_env = nullptr;
    leave();
    return (jint)v;
}
