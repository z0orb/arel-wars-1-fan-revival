// The bridge library (PLAN.md DES-06, DES-08): our libgameDSO.so for 64-bit ABIs.
// The generated trampolines (work/bridge/gen/exports.cpp, from tools/gen-exports.mjs) use Scope:
//   Scope s(env, &fn, "Java_..."); uint32_t a[] = {s.env(), s.borrow(cls), ...}; s.call(a, n);
#pragma once
#include <jni.h>
#include "guest.h"

namespace emu {

extern JNIEnv *g_env;
uint32_t jni_borrow(jobject o);   // a local handle for an argument from Java (not deleted at the end)
jobject jni_host(uint32_t h);
uint32_t jni_peak_locals();
void gl_real_init(bool debuggable);

// Debug log of PLAN.md TASK-E2.2 (debug builds only, g_dbg): one line per guest entry from Java (not
// NativeRender), per first use of a JNIEnv slot, per upcall, per RegisterNatives entry and per change of
// the game state probe. Lines start with the frame number and a tab. The file is arelemu-debug.log in the
// parent folder of the files folder. Lines before the files folder is known wait in memory.
extern bool g_dbg;
void dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dbg_set_dir(const char *files_root);
void dbg_flush();
void dbg_probe();                 // after each NativeRender: state, Gold, Cash and stage, on change
const char *jni_slot_name(uint32_t slot);

// One guest entry from Java. It takes the bridge lock, and frees the local handles and the lock at the end.
class Scope {
public:
    Scope(JNIEnv *env, uint32_t *fn, const char *name, bool render = false);
    ~Scope();
    uint32_t env() const;
    uint32_t borrow(jobject o) { return jni_borrow(o); }
    uint64_t call(const uint32_t *args, int n);
    // An object result: a new host local reference, which stays valid after the guest handles are freed.
    jobject ret_obj(uint32_t h);
private:
    JNIEnv *prev_;
    uint32_t *fn_;
    const char *name_;
    bool render_;
};

}  // namespace emu
