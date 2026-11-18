// Shims for the 74 imports of the guest (DES-05), from universal/oracle/src/shims.c.
// Each import resolves to "svc #index". The shim reads its AAPCS arguments from the guest
// registers and stack, works on guest memory and writes r0 (and r1).
// Runner back end: files are confined to files/ and assets/, time comes from the frame clock,
// sockets fail, malloc comes from the fixed zero-filled heap, and GL is counted, except
// glTexSubImage2D, which captures the 480x320 RGB565 frame.
#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "guest.h"

namespace emu {

char g_files_root[256], g_assets_root[256], g_files_root2[256];
bool g_app;
void (*g_gl_hook)(uint32_t gl_index);
uint8_t g_fb[512 * 512 * 2];
int g_fb_w, g_fb_h, g_fb_uploads;

// Guest data words in the data page (DES-05).
constexpr uint32_t D_ERRNO = DATA_BASE + 0x0000;
constexpr uint32_t D_GUARD = DATA_BASE + 0x0004;
constexpr uint32_t D_TM    = DATA_BASE + 0x0010;  // 44-byte bionic struct tm
constexpr uint32_t D_TZ    = DATA_BASE + 0x0040;  // "UTC"
constexpr uint32_t D_FILES = DATA_BASE + 0x0100;  // 32 FILE records of 16 bytes
void set_errno(int e) { W32(D_ERRNO, (uint32_t)e); }

// ---- files ----
struct OFILE { int fd, used, eof, err; };
static OFILE files[32];
static uint32_t file_addr(int i) { return D_FILES + 16u * i; }
static OFILE *file_of(uint32_t a, const char *what) {
    uint32_t i = (a - D_FILES) / 16;
    if (a < D_FILES || i >= 32 || (a - D_FILES) % 16 || !files[i].used) die("%s on a bad FILE 0x%x", what, a);
    return &files[i];
}
static void file_sync(int i) { memcpy(P(file_addr(i), 16), &files[i], 16); }

// Save-file path rules (DES-05): a path must be inside one of the roots. An empty root matches nothing.
// The runner has files/ and assets/. The bridge has the files folder of the app, as Java gave it
// (getAbsolueFilePath) and as its real path.
static int under(const char *p, const char *root) {
    size_t n = strlen(root);
    return n && !strncmp(p, root, n) && (p[n] == '/' || p[n] == 0);
}
static int path_ok(const char *p) {
    if (strstr(p, "..")) return 0;
    return under(p, g_files_root) || under(p, g_files_root2) || under(p, g_assets_root);
}

// Save backup (DES-14, REQ-17): before the guest writes, deletes or replaces Save%d.dat, Sng%d.dat
// or option.sav, copy the old file to <files>/arel-backup/<name>.1 and move older copies to .2 and .3.
// The newest copy is not made again when it is byte-equal to the file. A failure only writes a log line.
static int save_name(const char *p, const char **base) {
    const char *b = strrchr(p, '/'); b = b ? b + 1 : p; *base = b;
    unsigned n; char tail[8];
    if (!strcmp(b, "option.sav")) return 1;
    if (sscanf(b, "Save%u.dat%1s", &n, tail) == 1 && !strncmp(b, "Save", 4)) return 1;
    if (sscanf(b, "Sng%u.dat%1s", &n, tail) == 1 && !strncmp(b, "Sng", 3)) return 1;
    return 0;
}
static int read_all(const char *p, std::string &out) {
    int fd = open(p, O_RDONLY); if (fd < 0) return -1;
    char buf[8192]; ssize_t r; out.clear();
    while ((r = read(fd, buf, sizeof buf)) > 0) out.append(buf, (size_t)r);
    close(fd); return r < 0 ? -1 : 0;
}
static int write_all(const char *p, const std::string &d) {
    std::string tmp = std::string(p) + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) return -1;
    size_t put = 0;
    while (put < d.size()) { ssize_t r = write(fd, d.data() + put, d.size() - put); if (r <= 0) { close(fd); unlink(tmp.c_str()); return -1; } put += (size_t)r; }
    if (fsync(fd) || close(fd)) { unlink(tmp.c_str()); return -1; }
    return rename(tmp.c_str(), p);
}
static void save_backup(const char *path, const char *why) {
    const char *base;
    if (!save_name(path, &base)) return;
    int e = errno;
    std::string old;
    if (read_all(path, old)) { errno = e; return; }  // no old file: nothing to keep
    std::string dir = std::string(g_files_root) + "/arel-backup", b = dir + "/" + base;
    std::string newest;
    if (!read_all((b + ".1").c_str(), newest) && newest == old) { errno = e; return; }
    mkdir(dir.c_str(), 0700);
    rename((b + ".2").c_str(), (b + ".3").c_str());
    rename((b + ".1").c_str(), (b + ".2").c_str());
    if (write_all((b + ".1").c_str(), old)) olog("WARN backup of %s (%s) failed, errno %d", base, why, errno);
    else olog("backup %s (%s) %zu bytes", base, why, old.size());
    errno = e;
}

static uint32_t sh_fopen(const char *path, const char *mode) {
    int fl;
    if (!path_ok(path)) { olog("fopen DENIED %s %s", path, mode); set_errno(EACCES); return 0; }
    if (mode[0] == 'w' || mode[0] == 'a') save_backup(path, "fopen");
    if (mode[0] == 'r') fl = strchr(mode, '+') ? O_RDWR : O_RDONLY;
    else if (mode[0] == 'w') fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    else { set_errno(EINVAL); return 0; }
    int fd = open(path, fl, 0644);
    olog("fopen %s %s -> %s", path, mode, fd < 0 ? "fail" : "ok");
    if (fd < 0) { set_errno(errno); return 0; }
    for (int i = 0; i < 32; i++)
        if (!files[i].used) { files[i] = OFILE{fd, 1, 0, 0}; file_sync(i); return file_addr(i); }
    close(fd); die("fopen: too many open files");
}
static uint32_t sh_fread(uint32_t buf, uint32_t size, uint32_t n, uint32_t fa) {
    OFILE *f = file_of(fa, "fread");
    uint32_t want = size * n, got = 0;
    char *b = (char *)P(buf, want);
    while (got < want) {
        ssize_t r = read(f->fd, b + got, want - got);
        if (r <= 0) { if (r == 0) f->eof = 1; else f->err = 1; break; }
        got += (uint32_t)r;
    }
    file_sync((int)(f - files));
    return size ? got / size : 0;
}
static uint32_t sh_fwrite(uint32_t buf, uint32_t size, uint32_t n, uint32_t fa) {
    OFILE *f = file_of(fa, "fwrite");
    uint32_t want = size * n, put = 0;
    const char *b = (const char *)P(buf, want);
    while (put < want) {
        ssize_t r = write(f->fd, b + put, want - put);
        if (r <= 0) { f->err = 1; break; }
        put += (uint32_t)r;
    }
    file_sync((int)(f - files));
    return size ? put / size : 0;
}
static uint32_t sh_fseek(uint32_t fa, int32_t off, int whence) {
    OFILE *f = file_of(fa, "fseek"); f->eof = 0; file_sync((int)(f - files));
    return lseek(f->fd, off, whence) < 0 ? (uint32_t)-1 : 0;
}
static uint32_t sh_ftell(uint32_t fa) { return (uint32_t)lseek(file_of(fa, "ftell")->fd, 0, SEEK_CUR); }
static uint32_t sh_fclose(uint32_t fa) {
    OFILE *f = file_of(fa, "fclose"); close(f->fd); f->used = 0; file_sync((int)(f - files)); return 0;
}
// 32-bit bionic struct stat (104 bytes): st_mode at 16, st_size at 48 (checked with clang for armv7a).
static uint32_t sh_stat(const char *p, uint32_t buf) {
    if (!path_ok(p)) { olog("stat DENIED %s", p); set_errno(ENOENT); return (uint32_t)-1; }
    struct stat st; int r = stat(p, &st); if (r) set_errno(errno);
    if (!r) {
        uint8_t *b = (uint8_t *)P(buf, 104); memset(b, 0, 104);
        uint64_t dev = st.st_dev, rdev = st.st_rdev, ino = st.st_ino; int64_t size = st.st_size; uint64_t blocks = st.st_blocks;
        uint32_t ino32 = (uint32_t)ino, mode = st.st_mode, nlink = (uint32_t)st.st_nlink, uid = st.st_uid, gid = st.st_gid, blksize = (uint32_t)st.st_blksize;
        memcpy(b + 0, &dev, 8); memcpy(b + 12, &ino32, 4); memcpy(b + 16, &mode, 4); memcpy(b + 20, &nlink, 4);
        memcpy(b + 24, &uid, 4); memcpy(b + 28, &gid, 4); memcpy(b + 32, &rdev, 8); memcpy(b + 48, &size, 8);
        memcpy(b + 56, &blksize, 4); memcpy(b + 64, &blocks, 8);
        uint32_t t[6] = {(uint32_t)st.st_atim.tv_sec, (uint32_t)st.st_atim.tv_nsec, (uint32_t)st.st_mtim.tv_sec,
                         (uint32_t)st.st_mtim.tv_nsec, (uint32_t)st.st_ctim.tv_sec, (uint32_t)st.st_ctim.tv_nsec};
        memcpy(b + 72, t, 24); memcpy(b + 96, &ino, 8);
    }
    olog("stat %s -> %d", p, r); return (uint32_t)r;
}
static uint32_t sh_access(const char *p, int m) {
    if (!path_ok(p)) { olog("access DENIED %s", p); set_errno(ENOENT); return (uint32_t)-1; }
    int r = access(p, m); if (r) set_errno(errno);
    olog("access %s %d -> %d", p, m, r); return (uint32_t)r;
}
static uint32_t sh_unlink(const char *p) {
    if (!path_ok(p)) { olog("unlink DENIED %s", p); set_errno(EACCES); return (uint32_t)-1; }
    save_backup(p, "unlink");
    int r = unlink(p); if (r) set_errno(errno);
    olog("unlink %s -> %d", p, r); return (uint32_t)r;
}
static uint32_t sh_rename(const char *a, const char *b) {
    if (!path_ok(a) || !path_ok(b)) { olog("rename DENIED %s %s", a, b); set_errno(EACCES); return (uint32_t)-1; }
    save_backup(b, "rename");
    int r = rename(a, b); if (r) set_errno(errno);
    olog("rename %s %s -> %d", a, b, r); return (uint32_t)r;
}

// ---- output ----
static uint32_t log_printf(const char *fmt, ArgCursor &c) {
    char buf[1024]; int n = gformat(buf, sizeof buf, fmt, c);
    size_t l = strlen(buf); while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = 0;
    olog("printf %s", buf); return (uint32_t)n;
}
static uint32_t sh_vsprintf(uint32_t out, const char *fmt, uint32_t ap) {
    ArgCursor c = ArgCursor::va(ap);
    static char buf[65536];
    int n = gformat(buf, sizeof buf, fmt, c);
    if (n < 0 || n >= (int)sizeof buf) die("vsprintf result too long (%d)", n);
    memcpy(P(out, (uint32_t)n + 1), buf, (size_t)n + 1);
    return (uint32_t)n;
}

// ---- time: the frame clock and UTC in the runner, the real clock and time zone in the app ----
static uint64_t now_ms() {
    if (!g_app) return g_now_ms;
    struct timeval tv; gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
}
static uint32_t sh_time(uint32_t t) { uint32_t v = (uint32_t)(now_ms() / 1000); if (t) W32(t, v); return v; }
static uint32_t sh_gettimeofday(uint32_t tv) {
    if (!tv) return 0;
    if (g_app) { struct timeval h; gettimeofday(&h, nullptr); W32(tv, (uint32_t)h.tv_sec); W32(tv + 4, (uint32_t)h.tv_usec); }
    else { W32(tv, (uint32_t)(g_now_ms / 1000)); W32(tv + 4, (uint32_t)(g_now_ms % 1000) * 1000); }
    return 0;
}
static uint32_t sh_localtime(uint32_t t) {
    time_t v = (time_t)(int32_t)R32(t); struct tm tm;
    if (g_app) {
        localtime_r(&v, &tm);
        char z[16] = {0}; if (tm.tm_zone) strncpy(z, tm.tm_zone, 15);
        memcpy(P(D_TZ, 16), z, 16);
    } else gmtime_r(&v, &tm);
    int32_t w[9] = {tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_wday, tm.tm_yday, tm.tm_isdst};
    memcpy(P(D_TM, 44), w, 36); W32(D_TM + 36, (uint32_t)tm.tm_gmtoff); W32(D_TM + 40, D_TZ);
    return D_TM;
}

// ---- GL: counted and ignored, except the frame upload ----
enum { GL_N = 26 };
static const char *gl_names[GL_N] = {
    "glEnable", "glTexCoordPointer", "glHint", "glEnableClientState", "glMatrixMode", "glDisable", "glClear",
    "glClearColorx", "glLoadIdentity", "glActiveTexture", "glNormalPointer", "glTexEnvf", "glBindTexture",
    "glTexParameterx", "glDisableClientState", "glOrthof", "glTexImage2D", "glDrawArrays", "glTexSubImage2D",
    "glVertexPointer", "glViewport", "glColorPointer", "glGenTextures", "", "", ""};
static uint32_t gl_count[GL_N];
static uint32_t gl_next_tex = 1;
void shims_report(FILE *f) {
    for (int i = 0; i < GL_N; i++) if (gl_names[i][0]) fprintf(f, "%s %u\n", gl_names[i], gl_count[i]);
}

// ---- table: index = svc number ----
static const char *const names[] = {
    "malloc", "free", "memcpy", "memmove", "memset", "strlen", "strcmp", "strncmp", "strcpy", "strncpy",
    "strcat", "strncat", "strstr", "atoi", "fopen", "fread", "fwrite", "fseek", "ftell", "fclose",
    "stat", "access", "unlink", "rename", "printf", "vprintf", "putchar", "vsprintf", "time", "gettimeofday",
    "localtime", "ceil", "socket", "connect", "send", "recv", "select", "shutdown", "close", "fcntl",
    "inet_addr", "__errno", "__stack_chk_fail", "abort", "__cxa_guard_acquire", "__cxa_guard_release",
    "__gnu_Unwind_Find_exidx", "__cxa_begin_cleanup", "__cxa_call_unexpected", "__cxa_type_match",
    // GL: index 50 + gl_names index
    "glEnable", "glTexCoordPointer", "glHint", "glEnableClientState", "glMatrixMode", "glDisable", "glClear",
    "glClearColorx", "glLoadIdentity", "glActiveTexture", "glNormalPointer", "glTexEnvf", "glBindTexture",
    "glTexParameterx", "glDisableClientState", "glOrthof", "glTexImage2D", "glDrawArrays", "glTexSubImage2D",
    "glVertexPointer", "glViewport", "glColorPointer", "glGenTextures"};
constexpr int NNAMES = sizeof names / sizeof names[0];
constexpr int GL0 = 50;

int shim_index(const char *name) {
    for (int i = 0; i < NNAMES; i++) if (!strcmp(names[i], name)) return i;
    return -1;
}
uint32_t shim_data(const char *name) { return !strcmp(name, "__stack_chk_guard") ? D_GUARD : 0; }
void shims_init() { W32(D_GUARD, 0x5a17c0deu); memcpy(P(D_TZ, 4), "UTC", 4); }

static uint32_t gstrlen(uint32_t a) { return (uint32_t)strlen(S(a)); }

void shim_svc(uint32_t idx) {
    uint32_t a0 = arg(0), a1 = arg(1), a2 = arg(2), a3 = arg(3);
    switch (idx) {
    case 0: ret(heap_alloc(&g_heap, a0)); return;
    case 1: heap_free(&g_heap, a0); return;
    case 2: memcpy(P(a0, a2), P(a1, a2), a2); ret(a0); return;
    case 3: memmove(P(a0, a2), P(a1, a2), a2); ret(a0); return;
    case 4: memset(P(a0, a2), (int)a1, a2); ret(a0); return;
    case 5: ret(gstrlen(a0)); return;
    case 6: ret((uint32_t)strcmp(S(a0), S(a1))); return;
    case 7: {  // strncmp: the strings may lack a NUL inside n bytes, so check byte by byte
        int r = 0;
        for (uint32_t i = 0; i < a2; i++) {
            uint8_t x = R8(a0 + i), y = R8(a1 + i);
            if (x != y) { r = (int)x - (int)y; break; }
            if (!x) break;
        }
        ret((uint32_t)r); return;
    }
    case 8: { uint32_t n = gstrlen(a1) + 1; memmove(P(a0, n), H(a1), n); ret(a0); return; }
    case 9: {  // strncpy
        uint32_t i = 0;
        for (; i < a2 && R8(a1 + i); i++) W8(a0 + i, R8(a1 + i));
        for (; i < a2; i++) W8(a0 + i, 0);
        ret(a0); return;
    }
    case 10: { uint32_t d = gstrlen(a0), n = gstrlen(a1) + 1; memmove(P(a0 + d, n), H(a1), n); ret(a0); return; }
    case 11: {  // strncat
        uint32_t d = gstrlen(a0), i = 0;
        for (; i < a2 && R8(a1 + i); i++) W8(a0 + d + i, R8(a1 + i));
        W8(a0 + d + i, 0); ret(a0); return;
    }
    case 12: { const char *h = S(a0), *f = strstr(h, S(a1)); ret(f ? a0 + (uint32_t)(f - h) : 0); return; }
    case 13: ret((uint32_t)atoi(S(a0))); return;
    case 14: ret(sh_fopen(S(a0), S(a1))); return;
    case 15: ret(sh_fread(a0, a1, a2, a3)); return;
    case 16: ret(sh_fwrite(a0, a1, a2, a3)); return;
    case 17: ret(sh_fseek(a0, (int32_t)a1, (int)a2)); return;
    case 18: ret(sh_ftell(a0)); return;
    case 19: ret(sh_fclose(a0)); return;
    case 20: ret(sh_stat(S(a0), a1)); return;
    case 21: ret(sh_access(S(a0), (int)a1)); return;
    case 22: ret(sh_unlink(S(a0))); return;
    case 23: ret(sh_rename(S(a0), S(a1))); return;
    case 24: { ArgCursor c = ArgCursor::regs(1); ret(log_printf(S(a0), c)); return; }
    case 25: { ArgCursor c = ArgCursor::va(a1); ret(log_printf(S(a0), c)); return; }
    case 26: ret(a0); return;
    case 27: ret(sh_vsprintf(a0, S(a1), a2)); return;
    case 28: ret(sh_time(a0)); return;
    case 29: ret(sh_gettimeofday(a0)); return;
    case 30: ret(sh_localtime(a0)); return;
    case 31: {  // ceil(double), soft-float: the value is in r0:r1, the result too
        uint64_t b = a0 | (uint64_t)a1 << 32; double d; memcpy(&d, &b, 8); d = ceil(d); memcpy(&b, &d, 8); ret64(b); return;
    }
    case 32: olog("socket %d %d %d -> -1", (int)a0, (int)a1, (int)a2); set_errno(ENETUNREACH); ret((uint32_t)-1); return;
    case 33: case 34: case 35: case 36: case 37: olog("net call -> -1"); set_errno(ENETUNREACH); ret((uint32_t)-1); return;
    case 38: olog("close %d -> -1", (int)a0); set_errno(EBADF); ret((uint32_t)-1); return;
    case 39: olog("fcntl %d %d %d -> -1", (int)a0, (int)a1, (int)a2); set_errno(EBADF); ret((uint32_t)-1); return;
    case 40: ret(inet_addr(S(a0))); return;
    case 41: ret(D_ERRNO); return;
    case 42: die("__stack_chk_fail");
    case 43: die("abort() called by the game");
    case 44: ret((R32(a0) & 1) ? 0 : 1); return;
    case 45: W32(a0, 1); return;
    case 46: case 47: case 48: case 49: die("unwinder import %s called", names[idx]);
    default: break;
    }
    if (idx >= GL0 && idx < (uint32_t)NNAMES && g_gl_hook) { gl_count[idx - GL0]++; g_gl_hook(idx - GL0); return; }
    switch (idx) {
    case GL0 + 16:  // glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px)
        if (!gl_count[16]++) olog("glTexImage2D level %d ifmt 0x%x %dx%d fmt 0x%x type 0x%x", (int)a1, a2, (int)a3, (int)arg(4), arg(6), arg(7));
        return;
    case GL0 + 18: {  // glTexSubImage2D(target, level, x, y, w, h, fmt, type, px)
        int x = (int)a2, y = (int)a3, w = (int)arg(4), h = (int)arg(5); uint32_t fmt = arg(6), type = arg(7), px = arg(8);
        gl_count[18]++;
        if (fmt != 0x1907 || type != 0x8363 || x || y || w > 512 || h > 512 || w < 0 || h < 0)
            die("glTexSubImage2D with unexpected arguments %d %d %d %d 0x%x 0x%x", x, y, w, h, fmt, type);
        memcpy(g_fb, P(px, (uint32_t)(w * h * 2)), (size_t)w * h * 2);
        g_fb_w = w; g_fb_h = h; g_fb_uploads++;
        return;
    }
    case GL0 + 22:  // glGenTextures(n, ids)
        gl_count[22]++; for (uint32_t i = 0; i < a0; i++) W32(a1 + 4 * i, gl_next_tex++);
        return;
    default:
        if (idx >= GL0 && idx < (uint32_t)NNAMES) { gl_count[idx - GL0]++; return; }
        die("svc for unknown import %u", idx);
    }
}

}  // namespace emu
