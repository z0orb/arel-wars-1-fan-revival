// Real-JNI back end of the bridge (PLAN.md DES-07). The guest JNIEnv and JavaVM are tables of svc
// thunks in guest memory, with the same layout as the runner back end (jni_fake.cpp). Each slot that
// the oracle implements calls the real JNIEnv of the current host thread and converts the values.
// Every other slot writes a crash report with its number and stops.
//   handles: a guest jobject is 0x0e000000 + 4 * index into a host table of real references.
//            Local handles are freed when the outer native call returns (jni_end_call).
//   strings and byte arrays: copies in the guest Java heap, freed by the Release call.
//   method calls: arguments decoded from the signature of the method ID (varargs, va_list, jvalue).
//   exceptions: the real VM, as in v1.
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <jni.h>
#include <stdlib.h>
#include "bridge.h"

namespace emu {

JNIEnv *g_env;  // the JNIEnv of the thread that holds the bridge lock (bridge.cpp)

constexpr uint32_t D_ENVPTR = DATA_BASE + 0x1000;  // JNIEnv: points at D_ENVTAB
constexpr uint32_t D_ENVTAB = DATA_BASE + 0x1010;  // 240 words
constexpr uint32_t D_VMPTR  = DATA_BASE + 0x1400;  // JavaVM: points at D_VMTAB
constexpr uint32_t D_VMTAB  = DATA_BASE + 0x1410;  // 8 words
constexpr int ENV_SLOTS = 240;

// ---- handles ----
constexpr uint32_t H_BASE = 0x0e000000u, H_MAX = 0x00400000u;
enum Kind : uint8_t { FREE, LOCAL_OWNED, LOCAL_BORROWED, GLOBAL };
struct Slot { jobject ref; Kind kind; };
static std::vector<Slot> slots;
static std::vector<uint32_t> free_slots, locals, frames;
static uint32_t peak_locals;

static uint32_t new_slot(jobject o, Kind k) {
    uint32_t i;
    if (!free_slots.empty()) { i = free_slots.back(); free_slots.pop_back(); slots[i] = Slot{o, k}; }
    else { i = (uint32_t)slots.size(); if (i >= H_MAX) die("JNI: too many handles"); slots.push_back(Slot{o, k}); }
    if (k != GLOBAL) { locals.push_back(i); if (locals.size() > peak_locals) peak_locals = (uint32_t)locals.size(); }
    return H_BASE + 4u * i;
}
static uint32_t slot_index(uint32_t h) {
    uint32_t i = (h - H_BASE) / 4u;
    if (h < H_BASE || (h & 3u) || i >= slots.size() || slots[i].kind == FREE) die("JNI: bad handle 0x%x", h);
    return i;
}
static jobject host(uint32_t h) { return h ? slots[slot_index(h)].ref : nullptr; }
static void free_slot(uint32_t i) { slots[i] = Slot{nullptr, FREE}; free_slots.push_back(i); }
static uint32_t owned_local(jobject o) { return o ? new_slot(o, LOCAL_OWNED) : 0; }
uint32_t jni_borrow(jobject o) { return o ? new_slot(o, LOCAL_BORROWED) : 0; }
jobject jni_host(uint32_t h) { return host(h); }

void jni_end_call() {
    for (uint32_t i : locals) {
        Slot &s = slots[i];
        if (s.kind == LOCAL_OWNED) g_env->DeleteLocalRef(s.ref);
        if (s.kind == LOCAL_OWNED || s.kind == LOCAL_BORROWED) free_slot(i);
    }
    locals.clear();
    frames.clear();
}

// ---- copies of strings and arrays in the guest Java heap ----
static std::unordered_map<uint32_t, uint32_t> str_copies, arr_copies;  // guest address -> length

// ---- methods ----
struct method { jmethodID id; std::string name, sig; };
static std::vector<method> methods;

static uint32_t get_method(uint32_t c, const char *name, const char *sig, bool is_static) {
    jclass hc = (jclass)host(c);
    jmethodID id = is_static ? g_env->GetStaticMethodID(hc, name, sig) : g_env->GetMethodID(hc, name, sig);
    if (!id) { olog("Get%sMethodID %s %s -> null (exception pending)", is_static ? "Static" : "", name, sig); return 0; }
    for (size_t i = 0; i < methods.size(); i++) if (methods[i].id == id) return (uint32_t)(i + 1);
    methods.push_back(method{id, name, sig});
    olog("Get%sMethodID %s %s -> %zu", is_static ? "Static" : "", name, sig, methods.size());
    return (uint32_t)methods.size();
}
static method &method_of(uint32_t mid) {
    if (!mid || mid > methods.size()) die("JNI: bad method id 0x%x", mid);
    return methods[mid - 1];
}

// The files folder of the app is the result of getAbsolueFilePath (DES-05 path rules).
static void learn_files_root(jobject s) {
    if (!s) return;
    const char *c = g_env->GetStringUTFChars((jstring)s, nullptr);
    if (!c) return;
    if (strcmp(c, g_files_root)) {
        snprintf(g_files_root, sizeof g_files_root, "%s", c);
        char r[PATH_MAX];
        if (realpath(c, r)) snprintf(g_files_root2, sizeof g_files_root2, "%s", r); else g_files_root2[0] = 0;
        olog("files root %s (real path %s)", g_files_root, g_files_root2);
        dbg_set_dir(g_files_root2[0] ? g_files_root2 : g_files_root);
    }
    g_env->ReleaseStringUTFChars((jstring)s, c);
}

// ---- calls ----
enum Ret { R_OBJ, R_BOOL, R_BYTE, R_CHAR, R_SHORT, R_INT, R_LONG, R_FLOAT, R_DOUBLE, R_VOID };
#define SLOT(f) (offsetof(struct JNINativeInterface, f) / sizeof(void *))
struct call_slot { size_t slot; Ret t; int form; };  // form 0 varargs, 1 V, 2 A
static const call_slot call_slots[] = {
#define C3(T, R) {SLOT(CallStatic##T##Method), R, 0}, {SLOT(CallStatic##T##MethodV), R, 1}, {SLOT(CallStatic##T##MethodA), R, 2}
    C3(Object, R_OBJ), C3(Boolean, R_BOOL), C3(Byte, R_BYTE), C3(Char, R_CHAR), C3(Short, R_SHORT),
    C3(Int, R_INT), C3(Long, R_LONG), C3(Float, R_FLOAT), C3(Double, R_DOUBLE), C3(Void, R_VOID),
#undef C3
};

// Decodes the arguments of sig into jvalues. Varargs and va_list follow AAPCS (float becomes double,
// 8-byte values are 8-aligned). The A form reads 8-byte jvalue slots.
static int decode_args(const std::string &sig, int form, uint32_t a3, jvalue *out, int cap) {
    ArgCursor c = form == 0 ? ArgCursor::regs(3) : ArgCursor::va(a3);
    uint32_t jv = form == 2 ? a3 : 0;
    int n = 0;
    size_t p = sig.find('(');
    if (p == std::string::npos) die("JNI: bad signature %s", sig.c_str());
    for (p++; p < sig.size() && sig[p] != ')';) {
        if (n == cap) die("JNI: too many arguments in %s", sig.c_str());
        char t = sig[p];
        bool ref = t == 'L' || t == '[';
        if (t == '[') { while (sig[p] == '[') p++; if (sig[p] == 'L') p = sig.find(';', p); }
        else if (t == 'L') p = sig.find(';', p);
        if (p == std::string::npos) die("JNI: bad signature %s", sig.c_str());
        p++;
        jvalue &v = out[n++];
        v.j = 0;
        if (jv) {
            uint32_t lo = R32(jv), hi = R32(jv + 4); jv += 8;
            if (ref) v.l = host(lo);
            else if (t == 'J' || t == 'D') v.j = (jlong)(lo | (uint64_t)hi << 32);
            else if (t == 'Z') v.z = (jboolean)(lo & 0xff);
            else if (t == 'B') v.b = (jbyte)lo;
            else if (t == 'C') v.c = (jchar)lo;
            else if (t == 'S') v.s = (jshort)lo;
            else if (t == 'F') memcpy(&v.f, &lo, 4);
            else v.i = (jint)lo;
            continue;
        }
        if (ref) v.l = host(c.word());
        else if (t == 'J') v.j = (jlong)c.dword();
        else if (t == 'D') { uint64_t b = c.dword(); memcpy(&v.d, &b, 8); }
        else if (t == 'F') { uint64_t b = c.dword(); double d; memcpy(&d, &b, 8); v.f = (float)d; }
        else if (t == 'Z') v.z = (jboolean)(c.word() & 0xff);
        else if (t == 'B') v.b = (jbyte)c.word();
        else if (t == 'C') v.c = (jchar)c.word();
        else if (t == 'S') v.s = (jshort)c.word();
        else v.i = (jint)c.word();
    }
    return n;
}

// Upcall log (TASK-E2.2): name, signature, arguments and result of each static call.
// Numbers print as %d (J as %lld), strings in quotes, byte arrays as [B:length], other objects as L.
static std::string show_args(const std::string &sig, const jvalue *v, int n) {
    std::string o;
    char b[64];
    size_t p = sig.find('(') + 1;
    for (int i = 0; i < n && p < sig.size() && sig[p] != ')'; i++) {
        char t = sig[p];
        size_t q = p;
        if (t == '[') { while (sig[q] == '[') q++; if (sig[q] == 'L') q = sig.find(';', q); }
        else if (t == 'L') q = sig.find(';', q);
        std::string ty = sig.substr(p, q - p + 1);
        p = q + 1;
        o += i ? ", " : "";
        if (ty == "Ljava/lang/String;") {
            if (!v[i].l) { o += "null"; continue; }
            const char *c = g_env->GetStringUTFChars((jstring)v[i].l, nullptr);
            o += '"'; o += c ? c : "?"; o += '"';
            if (c) g_env->ReleaseStringUTFChars((jstring)v[i].l, c);
        } else if (ty == "[B") { snprintf(b, sizeof b, "[B:%d", v[i].l ? (int)g_env->GetArrayLength((jarray)v[i].l) : -1); o += b; }
        else if (t == 'L' || t == '[') o += v[i].l ? "L" : "null";
        else if (t == 'J') { snprintf(b, sizeof b, "%lld", (long long)v[i].j); o += b; }
        else if (t == 'F') { snprintf(b, sizeof b, "%g", (double)v[i].f); o += b; }
        else if (t == 'D') { snprintf(b, sizeof b, "%g", v[i].d); o += b; }
        else if (t == 'Z') o += v[i].z ? "1" : "0";
        else if (t == 'B') { snprintf(b, sizeof b, "%d", (int)v[i].b); o += b; }
        else if (t == 'C') { snprintf(b, sizeof b, "%d", (int)v[i].c); o += b; }
        else if (t == 'S') { snprintf(b, sizeof b, "%d", (int)v[i].s); o += b; }
        else { snprintf(b, sizeof b, "%d", (int)v[i].i); o += b; }
    }
    return o;
}
static jclass global_class(const char *n) {
    jclass l = g_env->FindClass(n);
    jclass g = l ? (jclass)g_env->NewGlobalRef(l) : nullptr;
    if (l) g_env->DeleteLocalRef(l);
    return g;
}
static std::string show_obj(jobject r) {
    static jclass c_str, c_bytes;
    if (!c_str) c_str = global_class("java/lang/String");
    if (!c_bytes) c_bytes = global_class("[B");
    if (!r) return "null";
    if (c_str && g_env->IsInstanceOf(r, c_str)) {
        const char *c = g_env->GetStringUTFChars((jstring)r, nullptr);
        std::string o = std::string("\"") + (c ? c : "?") + "\"";
        if (c) g_env->ReleaseStringUTFChars((jstring)r, c);
        return o;
    }
    if (c_bytes && g_env->IsInstanceOf(r, c_bytes)) return "[B:" + std::to_string(g_env->GetArrayLength((jarray)r));
    return "L";
}

static void call_static(const call_slot &cs, uint32_t a1, uint32_t a2, uint32_t a3) {
    method &m = method_of(a2);
    jclass hc = (jclass)host(a1);
    jvalue v[32];
    int n = decode_args(m.sig, cs.form, a3, v, 32);
    JNIEnv *e = g_env;
    std::string args = g_dbg ? show_args(m.sig, v, n) : std::string();
    std::string res;
    char b[64];
    switch (cs.t) {
    case R_OBJ: {
        jobject r = e->CallStaticObjectMethodA(hc, m.id, v);
        if (m.name == "getAbsolueFilePath") learn_files_root(r);
        if (g_dbg && !e->ExceptionCheck()) res = show_obj(r);
        ret(owned_local(r)); break;
    }
    case R_BOOL: { jboolean r = e->CallStaticBooleanMethodA(hc, m.id, v); res = r ? "1" : "0"; ret((uint8_t)r); break; }
    case R_BYTE: { jbyte r = e->CallStaticByteMethodA(hc, m.id, v); res = std::to_string((int)r); ret((uint32_t)(int32_t)r); break; }
    case R_CHAR: { jchar r = e->CallStaticCharMethodA(hc, m.id, v); res = std::to_string((int)r); ret((uint16_t)r); break; }
    case R_SHORT: { jshort r = e->CallStaticShortMethodA(hc, m.id, v); res = std::to_string((int)r); ret((uint32_t)(int32_t)r); break; }
    case R_INT: { jint r = e->CallStaticIntMethodA(hc, m.id, v); res = std::to_string(r); ret((uint32_t)r); break; }
    case R_LONG: { jlong r = e->CallStaticLongMethodA(hc, m.id, v); res = std::to_string((long long)r); ret64((uint64_t)r); break; }
    case R_FLOAT: { float f = e->CallStaticFloatMethodA(hc, m.id, v); snprintf(b, sizeof b, "%g", (double)f); res = b; uint32_t u; memcpy(&u, &f, 4); ret(u); break; }
    case R_DOUBLE: { double d = e->CallStaticDoubleMethodA(hc, m.id, v); snprintf(b, sizeof b, "%g", d); res = b; uint64_t u; memcpy(&u, &d, 8); ret64(u); break; }
    case R_VOID: e->CallStaticVoidMethodA(hc, m.id, v); res = "void"; break;
    }
    if (g_dbg) {
        bool ex = e->ExceptionCheck();
        dbg("upcall %s %s (%s) -> %s", m.name.c_str(), m.sig.c_str(), args.c_str(), ex ? "exception" : res.c_str());
    }
}

static void delete_local(uint32_t h) {
    if (!h) return;
    uint32_t i = slot_index(h);
    Slot &s = slots[i];
    if (s.kind == GLOBAL) { olog("WARN DeleteLocalRef on a global handle 0x%x", h); return; }
    g_env->DeleteLocalRef(s.ref);
    free_slot(i);
}

void jni_svc_env(uint32_t slot) {
    uint32_t a1 = arg(1), a2 = arg(2), a3 = arg(3);
    JNIEnv *e = g_env;
    static bool seen[ENV_SLOTS];
    if (g_dbg && slot < ENV_SLOTS && !seen[slot]) { seen[slot] = true; dbg("slot %u %s", slot, jni_slot_name(slot)); }
    for (const call_slot &c : call_slots)
        if (c.slot == slot) { call_static(c, a1, a2, a3); return; }
    switch (slot) {
    case SLOT(GetVersion): ret((uint32_t)e->GetVersion()); return;
    case SLOT(FindClass): { const char *n = S(a1); jclass c = e->FindClass(n); olog("FindClass %s -> %s", n, c ? "ok" : "null"); ret(owned_local(c)); return; }
    case SLOT(ExceptionOccurred): ret(owned_local(e->ExceptionOccurred())); return;
    case SLOT(ExceptionDescribe): e->ExceptionDescribe(); return;
    case SLOT(ExceptionClear): e->ExceptionClear(); return;
    case SLOT(ExceptionCheck): ret(e->ExceptionCheck() ? 1u : 0u); return;
    case SLOT(PushLocalFrame): {
        jint r = e->PushLocalFrame((jint)a1);
        if (!r) frames.push_back((uint32_t)locals.size());
        ret((uint32_t)r); return;
    }
    case SLOT(PopLocalFrame): {
        jobject r = e->PopLocalFrame(host(a1));
        if (!frames.empty()) {
            uint32_t mark = frames.back(); frames.pop_back();
            for (size_t k = mark; k < locals.size(); k++) {
                Slot &s = slots[locals[k]];
                if (s.kind == LOCAL_OWNED || s.kind == LOCAL_BORROWED) free_slot(locals[k]);
            }
            locals.resize(mark);
        }
        ret(owned_local(r)); return;
    }
    case SLOT(EnsureLocalCapacity): ret((uint32_t)e->EnsureLocalCapacity((jint)a1)); return;
    case SLOT(NewGlobalRef): { jobject o = host(a1); ret(o ? new_slot(e->NewGlobalRef(o), GLOBAL) : 0); return; }
    case SLOT(DeleteGlobalRef): {
        if (!a1) return;
        uint32_t i = slot_index(a1);
        if (slots[i].kind != GLOBAL) { olog("WARN DeleteGlobalRef on a local handle 0x%x", a1); return; }
        e->DeleteGlobalRef(slots[i].ref); free_slot(i);
        return;
    }
    case SLOT(DeleteLocalRef): delete_local(a1); return;
    case SLOT(IsSameObject): ret(e->IsSameObject(host(a1), host(a2)) ? 1u : 0u); return;
    case SLOT(GetObjectClass): ret(owned_local(e->GetObjectClass(host(a1)))); return;
    case SLOT(GetStaticMethodID): ret(get_method(a1, S(a2), S(a3), true)); return;
    case SLOT(GetMethodID): ret(get_method(a1, S(a2), S(a3), false)); return;
    case SLOT(NewStringUTF): ret(a1 ? owned_local(e->NewStringUTF(S(a1))) : 0); return;
    case SLOT(GetStringUTFLength): ret((uint32_t)e->GetStringUTFLength((jstring)host(a1))); return;
    case SLOT(GetStringLength): ret((uint32_t)e->GetStringLength((jstring)host(a1))); return;
    case SLOT(GetStringUTFChars): {
        jstring s = (jstring)host(a1);
        const char *c = e->GetStringUTFChars(s, nullptr);
        if (!c) { ret(0); return; }
        uint32_t n = (uint32_t)strlen(c);
        uint32_t g = heap_alloc(&g_jheap, n + 1);
        memcpy(P(g, n + 1), c, n + 1);
        e->ReleaseStringUTFChars(s, c);
        str_copies[g] = n;
        if (a2) W8(a2, 1);
        ret(g); return;
    }
    case SLOT(ReleaseStringUTFChars): {
        auto it = str_copies.find(a2);
        if (it == str_copies.end()) { olog("WARN ReleaseStringUTFChars of an unknown pointer 0x%x", a2); return; }
        str_copies.erase(it); heap_free(&g_jheap, a2);
        return;
    }
    case SLOT(GetArrayLength): ret((uint32_t)e->GetArrayLength((jarray)host(a1))); return;
    case SLOT(NewByteArray): ret(owned_local(e->NewByteArray((jsize)a1))); return;
    case SLOT(GetByteArrayElements): {
        jbyteArray arr = (jbyteArray)host(a1);
        uint32_t n = (uint32_t)e->GetArrayLength(arr);
        uint32_t g = heap_alloc(&g_jheap, n ? n : 1);
        if (n) e->GetByteArrayRegion(arr, 0, (jsize)n, (jbyte *)P(g, n));
        if (e->ExceptionCheck()) { heap_free(&g_jheap, g); ret(0); return; }
        arr_copies[g] = n;
        if (a2) W8(a2, 1);
        ret(g); return;
    }
    case SLOT(ReleaseByteArrayElements): {  // (array, elems, mode)
        auto it = arr_copies.find(a2);
        if (it == arr_copies.end()) { olog("WARN ReleaseByteArrayElements of an unknown pointer 0x%x", a2); return; }
        uint32_t n = it->second;
        if (a3 != JNI_ABORT && n) e->SetByteArrayRegion((jbyteArray)host(a1), 0, (jsize)n, (const jbyte *)P(a2, n));
        if (a3 != JNI_COMMIT) { arr_copies.erase(it); heap_free(&g_jheap, a2); }
        return;
    }
    case SLOT(GetByteArrayRegion): case SLOT(SetByteArrayRegion): {
        jbyteArray arr = (jbyteArray)host(a1);
        int32_t s = (int32_t)a2, n = (int32_t)a3; uint32_t buf = arg(4);
        jbyte *b = n > 0 ? (jbyte *)P(buf, (uint32_t)n) : (jbyte *)H(buf);
        if (slot == SLOT(GetByteArrayRegion)) e->GetByteArrayRegion(arr, s, n, b);
        else e->SetByteArrayRegion(arr, s, n, b);
        return;
    }
    case SLOT(RegisterNatives): {
        int32_t n = (int32_t)a3;
        for (int32_t k = 0; k < n && k < 64; k++)
        {
            const char *mn = S(R32(a2 + 12u * (uint32_t)k)), *ms = S(R32(a2 + 12u * (uint32_t)k + 4));
            olog("RegisterNatives %s %s (ignored)", mn, ms);
            dbg("RegisterNatives %s %s", mn, ms);
        }
        ret(0); return;
    }
    case SLOT(GetJavaVM): W32(a1, D_VMPTR); ret(0); return;
    default:
        dbg("slot-trap %u %s", slot, jni_slot_name(slot)); dbg_flush();
        die("JNI slot %u (offset 0x%x) not implemented", slot, slot * 4);
    }
}

// JavaVM: 3 DestroyJavaVM, 4 AttachCurrentThread, 5 DetachCurrentThread, 6 GetEnv, 7 AttachCurrentThreadAsDaemon.
// Each guest call runs on a host thread that the VM knows (a native method), so the guest env is valid.
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
uint32_t jni_peak_locals() { return peak_locals; }

}  // namespace emu
