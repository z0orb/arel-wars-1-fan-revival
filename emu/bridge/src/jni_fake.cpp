// Fake JavaVM and JNIEnv in guest memory: the runner back end (DES-07), from universal/oracle/src/jni.c.
// The JNIEnv table holds one svc thunk per slot. Implemented slots follow the oracle; every other
// slot stops with its number. Upcalls are dispatched by Java method name with v1 behaviour.
// Objects use the oracle layout in the guest Java heap, so the guest sees the same addresses.
#include <cstddef>
#include <fcntl.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>
#include "guest.h"

namespace emu {

int g_lang_english = 1;
int g_sound_log = 1;

#define ID_DEVICE  "001010123456780"
#define ID_ANDROID "a1e1a2f0c0de0102"
#define ID_PHONE   "15555550100"
#define ID_MAC     "02:00:00:a1:e1:02"
#define ID_SIM     "8999000000000000013"
#define ID_MODEL   "23021RAA2Y"
#define ID_OS      "15"

// Data page layout of this file.
constexpr uint32_t D_ENVPTR  = DATA_BASE + 0x1000;  // JNIEnv: points at D_ENVTAB
constexpr uint32_t D_ENVTAB  = DATA_BASE + 0x1010;  // 240 words
constexpr uint32_t D_VMPTR   = DATA_BASE + 0x1400;  // JavaVM: points at D_VMTAB
constexpr uint32_t D_VMTAB   = DATA_BASE + 0x1410;  // 8 words
constexpr uint32_t D_CLASSES = DATA_BASE + 0x1500;  // 16 class objects of 84 bytes
constexpr int ENV_SLOTS = 240;

// ---- objects: the oracle struct jo on 32 bits ----
// magic +0, kind +4, len +8, global +12, next_local +16, name[64] +20, data +84
enum { K_CLASS = 1, K_STRING, K_BYTES };
constexpr uint32_t JO_MAGIC = 0x4a4f424au, JO_HDR = 84;
static uint32_t locals;
static int nclasses;
static uint32_t jo_kind(uint32_t j) { return R32(j + 4); }
static uint32_t jo_len(uint32_t j) { return R32(j + 8); }
static uint32_t jo_data(uint32_t j) { return j + JO_HDR; }

static uint32_t obj(uint32_t j, uint32_t kind) {
    if (!j) die("JNI: null object where kind %u was expected", kind);
    if (R32(j) != JO_MAGIC || (kind && jo_kind(j) != kind)) die("JNI: bad object 0x%x (kind %u wanted)", j, kind);
    return j;
}
static uint32_t new_obj(uint32_t kind, uint32_t len) {
    uint32_t j = heap_alloc(&g_jheap, JO_HDR + len + 1);
    W32(j, JO_MAGIC); W32(j + 4, kind); W32(j + 8, len);
    W32(j + 16, locals); locals = j;
    return j;
}
static uint32_t new_string(const char *s) { uint32_t n = (uint32_t)strlen(s); uint32_t j = new_obj(K_STRING, n); memcpy(P(jo_data(j), n), s, n); return j; }
static uint32_t new_bytes(const void *p, uint32_t n) { uint32_t j = new_obj(K_BYTES, n); if (p) memcpy(P(jo_data(j), n), p, n); return j; }

void jni_end_call() {
    uint32_t j = locals; locals = 0;
    while (j) {
        uint32_t n = R32(j + 16);
        if (!R32(j + 12)) { W32(j, 0); heap_free(&g_jheap, j); } else W32(j + 16, 0);
        j = n;
    }
}

static uint32_t find_class(const char *name) {
    for (int i = 0; i < nclasses; i++) if (!strcmp(S(D_CLASSES + 84u * i + 20), name)) return D_CLASSES + 84u * i;
    if (nclasses == 16) die("JNI: too many classes");
    uint32_t c = D_CLASSES + 84u * nclasses++;
    W32(c, JO_MAGIC); W32(c + 4, K_CLASS); W32(c + 12, 1);
    strncpy((char *)P(c + 20, 64), name, 63);
    olog("FindClass %s", name);
    return c;
}
uint32_t jni_natives_class() { return find_class("com/gamevil/nexus2/Natives"); }

// ---- methods ----
struct method { char name[64], sig[96], cls[64]; int missing; };
static method methods[256];
static int nmethods;
static const char *missing[] = {"showReplyEventComponent", "hideReplyEventComponent", "getReconfirmForPurchase",
    "showMylSButton", "blinkSButton", "showComponent", "hideComponent", "NexusSetEngInput", "NexusSetBlockText",
    "NexusTouchSetUseCustomeGesture", "NexusTouchSetCheckGestureBuilder", "GFA_GetStringLength",
    "GFA_SetStringFromUnicode", "GFA_SetStringFromKSC5601", "PHONEMODEL", "PHONENUMBER", "OSVERSION", "DEVICETYPE",
    "MACADD", "DEVICEID", "ANDROIDID", "SIMSERIAL", nullptr};

static uint32_t get_method(uint32_t c, const char *name, const char *sig) {
    obj(c, K_CLASS);
    const char *cls = S(c + 20);
    for (int i = 0; i < nmethods; i++)
        if (!strcmp(methods[i].name, name) && !strcmp(methods[i].sig, sig) && !strcmp(methods[i].cls, cls)) return (uint32_t)(i + 1);
    if (nmethods == 256) die("JNI: too many methods");
    method *m = &methods[nmethods++];
    strncpy(m->name, name, 63); strncpy(m->sig, sig, 95); strncpy(m->cls, cls, 63);
    for (int i = 0; missing[i]; i++) if (!strcmp(missing[i], name)) m->missing = 1;
    olog("GetStaticMethodID %s.%s %s%s", cls, name, sig, m->missing ? " MISSING-IN-JAVA" : "");
    return (uint32_t)nmethods;
}

// ---- arguments: registers and stack, a guest va_list, or a guest jvalue array ----
struct args_t { ArgCursor c; uint32_t jv; int i; };
static uint32_t arg_i(args_t *x) { return x->jv ? R32(x->jv + 8u * x->i++) : x->c.word(); }
static const char *arg_str(args_t *x) { uint32_t j = arg_i(x); return j ? S(jo_data(obj(j, K_STRING))) : "(null)"; }

// ---- timers (spec/timing.md) ----
struct timer_t_ { uint64_t due; int idx, stamp, live; };
static timer_t_ timers[64];
static int ntimers, cur_timer = -1;
static uint32_t timer_seq[64], seq_next;
static void timer_set(int timeout, int idx, int stamp) {
    if (cur_timer >= 0 && timers[cur_timer].live && timers[cur_timer].idx == idx) timers[cur_timer].live = 0;
    int k = -1;
    for (int i = 0; i < ntimers; i++) if (!timers[i].live) { k = i; break; }
    if (k < 0) { if (ntimers == 64) die("too many timers"); k = ntimers++; }
    timers[k] = timer_t_{g_now_ms + (uint64_t)(timeout < 0 ? 0 : timeout), idx, stamp, 1};
    timer_seq[k] = seq_next++;
    cur_timer = k;
    olog("timer set %d ms index %d stamp %d", timeout, idx, stamp);
}
void timers_fire(void (*cb)(int, int)) {
    for (;;) {
        int best = -1;
        for (int i = 0; i < ntimers; i++)
            if (timers[i].live && timers[i].due <= g_now_ms && (best < 0 || timer_seq[i] < timer_seq[best])) best = i;
        if (best < 0) return;
        timers[best].live = 0;
        olog("timer fire index %d stamp %d", timers[best].idx, timers[best].stamp);
        cb(timers[best].idx, timers[best].stamp);
    }
}

// ---- upcalls ----
static int asset_path(const char *name, char *out, size_t n) {
    if (strstr(name, "..") || name[0] == '/') return 0;
    snprintf(out, n, "%s/%s", g_assets_root, name);
    return 1;
}
static uint32_t read_asset(const char *name) {
    char p[512];
    if (!asset_path(name, p, sizeof p)) return 0;
    int fd = open(p, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st; fstat(fd, &st);
    uint32_t j = new_bytes(nullptr, (uint32_t)st.st_size);
    if (read(fd, P(jo_data(j), (uint32_t)st.st_size), (size_t)st.st_size) != st.st_size) die("readAssete: short read on %s", p);
    close(fd);
    return j;
}

static uint64_t upcall(uint32_t mid, args_t *x) {
    int i = (int)mid - 1;
    if (i < 0 || i >= nmethods) die("JNI: bad method id 0x%x", mid);
    method *m = &methods[i];
    const char *n = m->name;
    if (m->missing) die("upcall to a Java method that does not exist: %s %s", n, m->sig);
#define IS(s) (!strcmp(n, s))
    if (IS("readAssete")) {
        const char *f = arg_str(x); uint32_t j = read_asset(f);
        olog("readAssete %s -> %d", f, j ? (int)jo_len(j) : -1);
        return j;
    }
    if (IS("isAssetExist")) {
        const char *f = arg_str(x); char p[512]; struct stat st; int r = 0;
        if (asset_path(f, p, sizeof p) && !stat(p, &st)) r = (int)st.st_size;
        olog("isAssetExist %s -> %d", f, r);
        return (uint32_t)r;
    }
    if (IS("getAbsolueFilePath")) return new_string(g_files_root);
    if (IS("OnSoundPlay")) {
        int id = (int)arg_i(x), vol = (int)arg_i(x), loop = (int)arg_i(x);
        if (g_sound_log) olog("sound play %d vol %d loop %d", id, vol, loop);
        return 0;
    }
    if (IS("OnStopSound")) { olog("sound stop"); return 0; }
    if (IS("OnVibrate")) { olog("vibrate %d", (int)arg_i(x)); return 0; }
    if (IS("SetSpeed")) { olog("setFrameSpeed %d", (int)arg_i(x)); return 0; }
    if (IS("OnAsyncTimerSet")) {
        int t = (int)arg_i(x), idx = (int)arg_i(x), st = 0;
        if (!strcmp(m->sig, "(III)V")) st = (int)arg_i(x);
        else olog("WARN two-argument OnAsyncTimerSet");
        timer_set(t, idx, st); return 0;
    }
    if (IS("OnUIStatusChange")) { olog("ui status %d", (int)arg_i(x)); return 0; }
    if (IS("OnEvent")) { olog("event %d", (int)arg_i(x)); return 0; }
    if (IS("setGameLanguage")) { olog("setGameLanguage %d", (int)arg_i(x)); return 0; }
    if (IS("isEnglish")) { olog("isEnglish -> %d", g_lang_english); return (uint32_t)g_lang_english; }
    if (IS("getGLOptionLinear")) { olog("getGLOptionLinear -> 1"); return 1; }
    if (IS("netConnect") || IS("isNetAvailable")) { olog("%s -> -1", n); return (uint32_t)-1; }
    const char *s = nullptr;
    if (IS("getDeviceID")) s = ID_DEVICE;
    else if (IS("getPhoneNumber") || IS("getOldPhoneNumber")) s = ID_PHONE;
    else if (IS("getAndroidID")) s = ID_ANDROID;
    else if (IS("getMacAddress")) s = ID_MAC;
    else if (IS("getSimSerialNumber")) s = ID_SIM;
    else if (IS("getPhoneModel")) s = ID_MODEL;
    else if (IS("getOsVersion")) s = ID_OS;
    else if (IS("getDeviceType")) s = "AD Default";
    if (s) { olog("%s -> %s", n, s); return new_bytes(s, (uint32_t)strlen(s)); }
    if (IS("getVersion")) { olog("getVersion"); return new_string("1.0.2"); }
    if (IS("confirmBuyItem")) { const char *t = arg_str(x); olog("confirmBuyItem %s %d", t, (int)arg_i(x)); return 0; }
    if (IS("showLoadingDialog") || IS("hideLoadingDialog") || IS("showHelpView") || IS("getTapjoyGPoint") ||
        IS("showTapjoyBtn") || IS("hideTapjoyBtn") || IS("showInquiryDialog")) { olog("%s", n); return 0; }
    if (!strncmp(n, "GFA_", 4)) { olog("GFA call %s %s", n, m->sig); return 0; }
    olog("UPCALL-UNKNOWN %s %s -> 0", n, m->sig);
    return 0;
#undef IS
}

// ---- JNIEnv slots ----
#define SLOT(f) (offsetof(struct JNINativeInterface, f) / sizeof(void *))
enum Ret { R_OBJ, R_BOOL, R_BYTE, R_CHAR, R_SHORT, R_INT, R_LONG, R_FLOAT, R_DOUBLE, R_VOID };
static void ret_typed(Ret t, uint64_t r) {
    switch (t) {
    case R_OBJ: case R_INT: ret((uint32_t)r); break;
    case R_BOOL: ret((uint8_t)r); break;
    case R_BYTE: ret((uint32_t)(int32_t)(int8_t)r); break;
    case R_CHAR: ret((uint16_t)r); break;
    case R_SHORT: ret((uint32_t)(int32_t)(int16_t)r); break;
    case R_LONG: ret64(r); break;
    case R_FLOAT: { float f = (float)(int32_t)r; uint32_t b; memcpy(&b, &f, 4); ret(b); break; }
    case R_DOUBLE: { double d = (double)(int32_t)r; uint64_t b; memcpy(&b, &d, 8); ret64(b); break; }
    case R_VOID: break;
    }
}
struct call_slot { size_t slot; Ret t; int form; };  // form 0 varargs, 1 V, 2 A
static const call_slot call_slots[] = {
#define C3(T, R) {SLOT(CallStatic##T##Method), R, 0}, {SLOT(CallStatic##T##MethodV), R, 1}, {SLOT(CallStatic##T##MethodA), R, 2}
    C3(Object, R_OBJ), C3(Boolean, R_BOOL), C3(Byte, R_BYTE), C3(Char, R_CHAR), C3(Short, R_SHORT),
    C3(Int, R_INT), C3(Long, R_LONG), C3(Float, R_FLOAT), C3(Double, R_DOUBLE), C3(Void, R_VOID),
#undef C3
};

static void delete_local(uint32_t o) {
    if (!o) return;
    uint32_t j = obj(o, 0);
    if (jo_kind(j) == K_CLASS) return;
    uint32_t prev = 0;
    for (uint32_t p = locals; p; prev = p, p = R32(p + 16))
        if (p == j) {
            if (prev) W32(prev + 16, R32(j + 16)); else locals = R32(j + 16);
            if (!R32(j + 12)) { W32(j, 0); heap_free(&g_jheap, j); }
            return;
        }
}

void jni_svc_env(uint32_t slot) {
    uint32_t a1 = arg(1), a2 = arg(2), a3 = arg(3);
    for (const call_slot &c : call_slots)
        if (c.slot == slot) {
            args_t x;
            x.i = 0; x.jv = 0;
            if (c.form == 0) x.c = ArgCursor::regs(3);
            else if (c.form == 1) x.c = ArgCursor::va(a3);
            else { x.c = ArgCursor::regs(4); x.jv = a3; }
            ret_typed(c.t, upcall(a2, &x));
            return;
        }
    switch (slot) {
    case SLOT(GetVersion): ret(JNI_VERSION_1_6); return;
    case SLOT(FindClass): ret(find_class(S(a1))); return;
    case SLOT(ExceptionOccurred): ret(0); return;
    case SLOT(ExceptionDescribe): case SLOT(ExceptionClear): return;
    case SLOT(ExceptionCheck): ret(0); return;
    case SLOT(PushLocalFrame): ret(0); return;
    case SLOT(PopLocalFrame): ret(a1); return;
    case SLOT(EnsureLocalCapacity): ret(0); return;
    case SLOT(NewGlobalRef): if (a1) { obj(a1, 0); W32(a1 + 12, R32(a1 + 12) + 1); } ret(a1); return;
    case SLOT(DeleteGlobalRef):
        if (a1 && R32(obj(a1, 0) + 12) && jo_kind(a1) != K_CLASS) W32(a1 + 12, R32(a1 + 12) - 1);
        return;
    case SLOT(DeleteLocalRef): delete_local(a1); return;
    case SLOT(IsSameObject): ret(a1 == a2); return;
    case SLOT(GetObjectClass): ret(find_class("java/lang/Object")); return;
    case SLOT(GetStaticMethodID): case SLOT(GetMethodID): ret(get_method(a1, S(a2), S(a3))); return;
    case SLOT(NewStringUTF): ret(a1 ? new_string(S(a1)) : 0); return;
    case SLOT(GetStringUTFLength): case SLOT(GetStringLength): ret(jo_len(obj(a1, K_STRING))); return;
    case SLOT(GetStringUTFChars): if (a2) W8(a2, 0); ret(jo_data(obj(a1, K_STRING))); return;
    case SLOT(ReleaseStringUTFChars): return;
    case SLOT(GetArrayLength): ret(jo_len(obj(a1, K_BYTES))); return;
    case SLOT(NewByteArray): ret(new_bytes(nullptr, a1)); return;
    case SLOT(GetByteArrayElements): if (a2) W8(a2, 0); ret(jo_data(obj(a1, K_BYTES))); return;
    case SLOT(ReleaseByteArrayElements): return;
    case SLOT(GetByteArrayRegion): case SLOT(SetByteArrayRegion): {
        uint32_t j = obj(a1, K_BYTES), buf = arg(4); int32_t s = (int32_t)a2, n = (int32_t)a3;
        if (s < 0 || n < 0 || (uint32_t)(s + n) > jo_len(j))
            die("%sByteArrayRegion out of range %d+%d of %u", slot == SLOT(GetByteArrayRegion) ? "Get" : "Set", s, n, jo_len(j));
        if (slot == SLOT(GetByteArrayRegion)) memcpy(P(buf, (uint32_t)n), H(jo_data(j) + (uint32_t)s), (size_t)n);
        else memcpy(P(jo_data(j) + (uint32_t)s, (uint32_t)n), P(buf, (uint32_t)n), (size_t)n);
        return;
    }
    case SLOT(RegisterNatives): olog("RegisterNatives %d", (int)a3); ret(0); return;
    case SLOT(GetJavaVM): W32(a1, D_VMPTR); ret(0); return;
    default: die("JNI slot %u (offset 0x%x) not implemented", slot, slot * 4);
    }
}

// JavaVM: 3 DestroyJavaVM, 4 AttachCurrentThread, 5 DetachCurrentThread, 6 GetEnv, 7 AttachCurrentThreadAsDaemon
void jni_svc_vm(uint32_t slot) {
    switch (slot) {
    case 3: case 5: ret(0); return;
    case 4: case 7: case 6: W32(arg(1), D_ENVPTR); ret(0); return;
    default: die("JavaVM slot %u not implemented", slot);
    }
}

void jni_init() {
    W32(D_ENVPTR, D_ENVTAB);
    for (int i = 0; i < ENV_SLOTS; i++) W32(D_ENVTAB + 4u * i, thunk(SVC_ENV + (uint32_t)i));
    W32(D_VMPTR, D_VMTAB);
    for (int i = 0; i < 8; i++) W32(D_VMTAB + 4u * i, i >= 3 ? thunk(SVC_VM + (uint32_t)i) : 0);
}
uint32_t jni_vm() { return D_VMPTR; }
uint32_t jni_env() { return D_ENVPTR; }

}  // namespace emu
