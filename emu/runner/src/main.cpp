// arel-emu: the runner (PLAN.md DES-13). Runs the guest through the core with the input scripts
// of the oracle, and writes the same outputs as the oracle (universal/oracle/src/main.c).
//   arel-emu -so LIB -assets DIR -files DIR -out DIR [-frames N] [-script FILE]
//            [-png LIST] [-probe LIST] [-t0 MS] [-lang eng|kor] [-quiet-sound] [-prof FUNCS]
//   -prof FUNCS: FUNCS lists mangled names; prof.tsv gets frame, guest us, us inside them, calls.
//   -native SPEC: the native replacements (DES-12): "all" (default), "none", or names and families
//            with "-" to turn one off ("all,-leaves"). AREL_NATIVE_MUT=N turns on mutation N.
//   -vec CASES [-vec-dump LIST] [-peek A:N,...]: vector mode (vec.cpp) in place of a session.
//   -native-list: print every replacement (family, name) and stop.
//   LIST is "10,20,300-400", "every:K" or "all".
// Outputs in DIR: frames.tsv (frame, FNV-1a 64 hash of the uploaded 480x320 RGB565 frame,
// uploads so far, guest time of NativeRender in microseconds), log.txt, probes.jsonl,
// png/NNNNN.rgb565, summary.txt.
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "../../bridge/src/guest.h"
#include "../../bridge/src/native/native.h"
#include "vec.h"

using namespace emu;

static uint64_t t0 = 1700000000000ull;
static char out_dir[256];
static const char *so_path, *script_path, *png_spec, *probe_spec;
static int nframes = 600;
static const char *native_spec = "all", *vec_path, *vec_dump, *peek_spec;
constexpr uint64_t FRAME_MS = 47;

static int in_list(const char *spec, uint32_t n) {
    if (!spec) return 0;
    if (!strcmp(spec, "all")) return 1;
    if (!strncmp(spec, "every:", 6)) { int k = atoi(spec + 6); return k > 0 && n % (uint32_t)k == 0; }
    const char *p = spec;
    while (*p) {
        uint32_t a = (uint32_t)strtoul(p, (char **)&p, 10), b = a;
        if (*p == '-') { p++; b = (uint32_t)strtoul(p, (char **)&p, 10); }
        if (n >= a && n <= b) return 1;
        if (*p == ',') p++; else break;
    }
    return 0;
}

// ---- exports ----
static uint32_t ex(const char *n) {
    char b[128]; snprintf(b, sizeof b, "Java_com_gamevil_nexus2_Natives_%s", n);
    uint32_t a = so_sym(b); if (!a) die("export %s not found", b); return a;
}
static void call_v(const char *n) { uint32_t a[2] = {jni_env(), jni_natives_class()}; gcall(ex(n), a, 2); jni_end_call(); }
static void call_ii(const char *n, int x, int y) { uint32_t a[4] = {jni_env(), jni_natives_class(), (uint32_t)x, (uint32_t)y}; gcall(ex(n), a, 4); jni_end_call(); }
static void clet_event(int t, int x, int y, int z) {
    olog("handleCletEvent %d %d %d %d", t, x, y, z);
    uint32_t a[6] = {jni_env(), jni_natives_class(), (uint32_t)t, (uint32_t)x, (uint32_t)y, (uint32_t)z};
    gcall(ex("handleCletEvent"), a, 6); jni_end_call();
}
static void timer_cb(int idx, int stamp) { call_ii("NativeAsyncTimerCallBackTimeStemp", idx, stamp); }

// ---- input script (the oracle format) ----
struct line_t { uint32_t frame; char cmd[16]; int a, b, c, d; };
static line_t *lines; static int nlines;
static void load_script(const char *p) {
    FILE *f = fopen(p, "r"); if (!f) die("cannot open script %s", p);
    char buf[256]; int cap = 0;
    while (fgets(buf, sizeof buf, f)) {
        char *s = buf; while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == '\r' || !*s) continue;
        if (nlines == cap) { cap = cap ? cap * 2 : 64; lines = (line_t *)realloc(lines, (size_t)cap * sizeof *lines); }
        line_t *l = &lines[nlines]; memset(l, 0, sizeof *l);
        if (sscanf(s, "%u %15s %d %d %d %d", &l->frame, l->cmd, &l->a, &l->b, &l->c, &l->d) < 2) die("bad script line: %s", s);
        if (nlines && l->frame < lines[nlines - 1].frame) die("script frames must not go backwards: %s", s);
        nlines++;
    }
    fclose(f);
}
#define QCAP 64
static struct { int t, a, b, c; } q[QCAP];
static int qh, qn, resume_pending = 1, paused, stop_at = -1;
static void enqueue(int t, int a, int b, int c) {
    if (qn == QCAP) { olog("WARN input queue full, cleared"); qn = 0; }
    int k = (qh + qn) % QCAP; q[k].t = t; q[k].a = a; q[k].b = b; q[k].c = c; qn++;
}
static int want_png, want_probe;
static void run_line(line_t *l) {
    const char *c = l->cmd;
    olog("script %s %d %d", c, l->a, l->b);
    if (!strcmp(c, "tap") || !strcmp(c, "press")) {
        enqueue(23, l->a, l->b, 0);
        if (resume_pending) { resume_pending = 0; clet_event(100002, 0, 0, 0); }
        if (!strcmp(c, "tap")) enqueue(24, l->a, l->b, 0);
    } else if (!strcmp(c, "release")) enqueue(24, l->a, l->b, 0);
    else if (!strcmp(c, "move")) enqueue(25, l->a, l->b, 0);
    else if (!strcmp(c, "keydown")) enqueue(2, l->a, 0, 0);
    else if (!strcmp(c, "keyup")) enqueue(3, l->a, 0, 0);
    else if (!strcmp(c, "back")) enqueue(2, -8, 0, 0);
    else if (!strcmp(c, "event")) enqueue(l->a, l->b, l->c, l->d);
    else if (!strcmp(c, "direct")) clet_event(l->a, l->b, l->c, l->d);
    else if (!strcmp(c, "pause")) { paused = 1; call_v("NativePauseClet"); }
    else if (!strcmp(c, "resume")) { paused = 0; call_v("NativeResumeClet"); call_v("InitializeJNIGlobalRef"); resume_pending = 1; }
    else if (!strcmp(c, "png")) want_png = 1;
    else if (!strcmp(c, "probe")) want_probe = 1;
    else if (!strcmp(c, "end")) stop_at = (int)l->frame;
    else die("unknown script command %s", c);
}

// ---- outputs ----
static uint64_t fb_hash() {
    uint64_t h = 1469598103934665603ull;  // the oracle basis (universal/spec/DIVERGENCES.md #2)
    size_t n = (size_t)g_fb_w * g_fb_h * 2;
    for (size_t i = 0; i < n; i++) { h ^= g_fb[i]; h *= 1099511628211ull; }
    return h;
}
static void write_png() {
    char p[400]; snprintf(p, sizeof p, "%s/png/%05u.rgb565", out_dir, g_frame);
    FILE *f = fopen(p, "wb"); if (!f) die("cannot write %s", p);
    uint16_t hdr[2] = {(uint16_t)g_fb_w, (uint16_t)g_fb_h};
    fwrite(hdr, 2, 2, f); fwrite(g_fb, 2, (size_t)g_fb_w * g_fb_h, f); fclose(f);
}
static int readable(uint32_t a, uint32_t n) {
    return (a >= SO_BASE && a + n <= so_end()) || (a >= HEAP_BASE && a + n <= g_heap.top);
}
static int32_t RI(uint32_t a) { return (int32_t)R32(a); }
static int16_t RS(uint32_t a) { return (int16_t)R16(a); }
static uint32_t singleton(const char *cls) {
    char n[128]; snprintf(n, sizeof n, "_ZN12CGsSingletonI%zu%sE13ms_pSingletonE", strlen(cls), cls);
    uint32_t a = so_sym(n);
    return a ? R32(a) : 0;
}
static FILE *probes;
static void write_probe() {
    if (!probes) { char p[400]; snprintf(p, sizeof p, "%s/probes.jsonl", out_dir); probes = fopen(p, "w"); }
    FILE *f = probes;
    uint32_t getf = so_sym("_Z12GxGetFrameT1v");
    uint32_t app = getf ? (uint32_t)gcall0(getf) : 0;
    uint32_t sh = singleton("CPdSharing"), inv = singleton("CInventory"), um = singleton("CUnitMgr"), pm = singleton("CProjectileMgr");
    fprintf(f, "{\"frame\":%u", g_frame);
    fprintf(f, ",\"state\":%d,\"pending\":%d", sh && readable(sh, 0x100) ? RI(sh + 8) : -1, sh && readable(sh, 0x100) ? RI(sh + 4) : -1);
    fprintf(f, ",\"tick\":%d", app && readable(app, 0x100) ? RI(app + 0xd4) : -1);
    if (inv && readable(inv, 0x700)) fprintf(f, ",\"gold\":%d,\"cash\":%d,\"stage\":%d", RI(inv + 0x320), RI(inv + 0x328), RI(inv + 0x330));
    else fputs(",\"gold\":null,\"cash\":null,\"stage\":null", f);
    fputs(",\"units\":[", f);
    if (um && readable(um, 0x88)) {
        uint32_t arr = R32(um + 0x84);
        if (arr && readable(arr, 12)) {
            uint32_t el = R32(arr + 4); int n = RI(arr + 8);
            for (int i = 0; i < n && el && readable(el + 4u * i, 4); i++) {
                uint32_t u = R32(el + 4u * i);
                if (!readable(u, 0x190)) continue;
                uint32_t pos = R32(u + 0x24);
                fprintf(f, "%s{\"i\":%d,\"x\":%d,\"y\":%d,\"z\":%d,\"hp\":%d,\"maxHp\":%d}", i ? "," : "", i,
                        readable(pos, 0x1a) ? RS(pos + 0x14) : 0, readable(pos, 0x1a) ? RS(pos + 0x16) : 0,
                        readable(pos, 0x1a) ? RS(pos + 0x18) : 0, RI(u + 0x74), RI(u + 0x7c));
            }
        }
    }
    fputs("]", f);
    int np = -1;
    if (pm && readable(pm, 8)) { uint32_t arr = R32(pm + 4); if (arr && readable(arr, 12)) np = RI(arr + 8); }
    fprintf(f, ",\"projectiles\":%d,\"rng\":%u}\n", np, R32(VA(0x62385c)));
    fflush(f);
}

// ---- prof (DES-13): time inside a set of guest functions, top-level entries only ----
// An entry hook (no code change) saves lr and points it at the exit thunk; the exit thunk puts lr back.
static uint64_t mono_us();
static const char *prof_path;
static uint32_t prof_lr[4096]; static int prof_sp;
static uint64_t prof_t0, prof_in_us, prof_calls;
static void prof_svc(uint32_t n) {
    if (n == SVC_PROF_EXIT) {
        if (prof_sp <= 0) die("prof: exit without entry");
        g_core->reg(14) = prof_lr[--prof_sp];
        if (prof_sp == 0) prof_in_us += mono_us() - prof_t0;
        return;
    }
    if (prof_sp == 4096) die("prof: nesting too deep");
    if (prof_sp == 0) prof_t0 = mono_us();
    prof_lr[prof_sp++] = g_core->reg(14);
    g_core->reg(14) = thunk(SVC_PROF_EXIT);
    prof_calls++;
}
static void prof_init() {
    FILE *f = fopen(prof_path, "r"); if (!f) die("cannot open %s", prof_path);
    char b[512]; uint32_t k = 0;
    while (fgets(b, sizeof b, f)) {
        b[strcspn(b, "\r\n")] = 0; if (!b[0]) continue;
        uint32_t a = so_sym(b); if (!a) die("prof: symbol %s not found", b);
        g_core->hook_entry(a, SVC_PROF_ENTRY + k++);
    }
    fclose(f);
    g_prof_svc = prof_svc;
    g_core->clear_cache();
    olog("prof: %u functions hooked", k);
}

static void on_alarm(int) { die("watchdog: a frame ran for more than 30 seconds"); }
static void arm_watchdog(int sec) { struct itimerval it = {{0, 0}, {sec, 0}}; setitimer(ITIMER_REAL, &it, nullptr); }

static void boot() {
    // GsSleep spins on the clock, which only moves per frame (spec/timing.md). Runner only (DES-05).
    static const so_patch_t patches[] = {{0x5dab8, 0xb570, 0x4770, "GsSleep: push {r4,r5,r6,lr} -> bx lr"}};
    so_load(so_path, patches, 1);
    olog("loaded %s, text end 0x%x, image end 0x%x", so_path, so_text_end(), so_end());
    nat::install(native_spec);
    so_run_init();
}
static void boot_java() {
    uint32_t a[2] = {jni_vm(), 0};
    uint32_t v = (uint32_t)gcall(so_sym("JNI_OnLoad"), a, 2);
    olog("JNI_OnLoad -> 0x%x", v);
    uint32_t b[3] = {jni_env(), jni_natives_class(), 0};
    gcall(ex("NativeIsNexusOne"), b, 3); jni_end_call();
    call_v("NativeResumeClet");
    call_v("InitializeJNIGlobalRef");
    call_ii("NativeInitWithBufferSize", 480, 320);
    call_ii("NativeInitDeviceInfo", 480, 320);
    call_ii("NativeResize", 1620, 1080);
}
static uint64_t mono_us() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }

static void session(FILE *frames_out) {
    boot();
    FILE *prof_out = nullptr;
    if (prof_path) {
        prof_init();
        char p[400]; snprintf(p, sizeof p, "%s/prof.tsv", out_dir); prof_out = fopen(p, "w");
    }
    g_now_ms = t0;
    boot_java();
    g_phase = 1;
    int li = 0;
    for (g_frame = 0; g_frame < (uint32_t)nframes; g_frame++) {
        arm_watchdog(30);
        g_now_ms = t0 + FRAME_MS * g_frame;
        while (li < nlines && lines[li].frame == g_frame) run_line(&lines[li++]);
        if (stop_at >= 0) break;
        long guest_us = 0;
        if (!paused) {
            timers_fire(timer_cb);
            if (qn) { int t = q[qh].t, a = q[qh].a, b = q[qh].b, c = q[qh].c; qh = (qh + 1) % QCAP; qn--; clet_event(t, a, b, c); }
            prof_in_us = prof_calls = 0;
            uint64_t s = mono_us();
            call_v("NativeRender");
            guest_us = (long)(mono_us() - s);
            if (prof_out) fprintf(prof_out, "%u\t%ld\t%llu\t%llu\n", g_frame, guest_us, (unsigned long long)prof_in_us, (unsigned long long)prof_calls);
        }
        fprintf(frames_out, "%u\t%016llx\t%d\t%ld%s\n", g_frame, (unsigned long long)fb_hash(), g_fb_uploads, guest_us, paused ? "\tpaused" : "");
        if (want_png || in_list(png_spec, g_frame)) write_png();
        if (want_probe || in_list(probe_spec, g_frame)) write_probe();
        want_png = want_probe = 0;
    }
    arm_watchdog(0);
    if (prof_out) fclose(prof_out);
}

int main(int argc, char **argv) {
    const char *assets = nullptr, *files = nullptr, *lang = "eng";
    for (int i = 1; i < argc; i++) {
        const char *k = argv[i], *v = i + 1 < argc ? argv[i + 1] : nullptr;
#define OPT(s) (!strcmp(k, s) && v && ++i)
        if (OPT("-so")) so_path = v;
        else if (OPT("-assets")) assets = v;
        else if (OPT("-files")) files = v;
        else if (OPT("-out")) snprintf(out_dir, sizeof out_dir, "%s", v);
        else if (OPT("-frames")) nframes = atoi(v);
        else if (OPT("-script")) script_path = v;
        else if (OPT("-png")) png_spec = v;
        else if (OPT("-probe")) probe_spec = v;
        else if (OPT("-t0")) t0 = strtoull(v, nullptr, 10);
        else if (OPT("-lang")) lang = v;
        else if (OPT("-prof")) prof_path = v;
        else if (OPT("-native")) native_spec = v;
        else if (OPT("-vec")) vec_path = v;
        else if (OPT("-vec-dump")) vec_dump = v;
        else if (OPT("-peek")) peek_spec = v;
        else if (!strcmp(k, "-native-list")) { nat::list(stdout); return 0; }
        else if (!strcmp(k, "-quiet-sound")) g_sound_log = 0;
        else { fprintf(stderr, "unknown or incomplete option %s\n", k); return 2; }
#undef OPT
    }
    if (!so_path || !out_dir[0] || !assets || !files) { fprintf(stderr, "need -so, -out, -assets and -files\n"); return 2; }
    g_lang_english = strcmp(lang, "kor") != 0;
    mkdir(out_dir, 0755);
    char p[400]; snprintf(p, sizeof p, "%s/png", out_dir); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/log.txt", out_dir); g_log = fopen(p, "w");
    if (!realpath(assets, g_assets_root)) { fprintf(stderr, "bad -assets\n"); return 2; }
    if (!realpath(files, g_files_root)) { fprintf(stderr, "bad -files\n"); return 2; }
    signal(SIGALRM, on_alarm);

    mem_init();
    core_init();
    shims_init();
    jni_init();
    heap_init(&g_heap, HEAP_BASE, HEAP_SIZE, "game heap");
    heap_init(&g_jheap, JHEAP_BASE, JHEAP_SIZE, "java heap");
    if (script_path) load_script(script_path);

    snprintf(p, sizeof p, "%s/frames.tsv", out_dir);
    FILE *frames_out = fopen(p, "w");
    uint64_t s = mono_us();
    if (vec_path) {
        boot();
        g_now_ms = t0;
        boot_java();
        g_phase = 1;
        if (peek_spec) vec_peek(peek_spec, out_dir);
        vec_run(vec_path, vec_dump, out_dir);
    } else session(frames_out);
    fclose(frames_out);
    snprintf(p, sizeof p, "%s/summary.txt", out_dir);
    FILE *sm = fopen(p, "w");
    fprintf(sm, "frames %u\nuploads %d\nheap_used 0x%x\nheap_peak_live 0x%x\nheap_allocs %u\nheap_frees %u\njheap_used 0x%x\n",
            g_frame, g_fb_uploads, g_heap.top - g_heap.base, g_heap.peak, g_heap.allocs, g_heap.frees, g_jheap.top - g_jheap.base);
    shims_report(sm);
    nat::report(sm);
    fprintf(sm, "wall_ms %llu\nstatus ok\n", (unsigned long long)((mono_us() - s) / 1000));
    fclose(sm);
    if (g_log) fclose(g_log);
    if (probes) fclose(probes);
    return 0;
}
