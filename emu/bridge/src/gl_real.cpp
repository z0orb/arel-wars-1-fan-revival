// GL forwarding of the bridge (PLAN.md DES-09): the 23 GL imports of the guest go to the real GLES 1
// context that the v1 shell makes current on the GL thread. Values go through unchanged. Pointers are
// guest addresses: they become host addresses (block base + guest address). Guest memory never moves,
// so client arrays stay valid until glDrawArrays. The guest uses the soft-float ABI (armeabi), so a
// float argument is its bit pattern in a core register or a stack word.
#include <GLES/gl.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include "guest.h"

namespace emu {

static bool hash_log;   // debug.arelemu.hash=1: log the hash of each uploaded frame (debug builds)
static bool warned_sub;

static float F(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }
static const void *ptr(uint32_t a) { return a ? P(a, 1) : nullptr; }

static uint64_t fnv(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;  // the oracle basis (universal/spec/DIVERGENCES.md #2)
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// Index order: gl_names in shims.cpp.
static void gl_call(uint32_t i) {
    uint32_t a0 = arg(0), a1 = arg(1), a2 = arg(2), a3 = arg(3);
    switch (i) {
    case 0: glEnable(a0); return;
    case 1: glTexCoordPointer((GLint)a0, a1, (GLsizei)a2, ptr(a3)); return;
    case 2: glHint(a0, a1); return;
    case 3: glEnableClientState(a0); return;
    case 4: glMatrixMode(a0); return;
    case 5: glDisable(a0); return;
    case 6: glClear(a0); return;
    case 7: glClearColorx((GLfixed)a0, (GLfixed)a1, (GLfixed)a2, (GLfixed)a3); return;
    case 8: glLoadIdentity(); return;
    case 9: glActiveTexture(a0); return;
    case 10: glNormalPointer(a0, (GLsizei)a1, ptr(a2)); return;
    case 11: glTexEnvf(a0, a1, F(a2)); return;
    case 12: glBindTexture(a0, a1); return;
    case 13: glTexParameterx(a0, a1, (GLfixed)a2); return;
    case 14: glDisableClientState(a0); return;
    case 15: glOrthof(F(a0), F(a1), F(a2), F(a3), F(arg(4)), F(arg(5))); return;
    case 16: {  // glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px)
        uint32_t px = arg(8);
        glTexImage2D(a0, (GLint)a1, (GLint)a2, (GLsizei)a3, (GLsizei)arg(4), (GLint)arg(5), arg(6), arg(7), ptr(px));
        return;
    }
    case 17: glDrawArrays(a0, (GLint)a1, (GLsizei)a2); return;
    case 18: {  // glTexSubImage2D(target, level, x, y, w, h, fmt, type, px)
        int x = (int)a2, y = (int)a3, w = (int)arg(4), h = (int)arg(5); uint32_t fmt = arg(6), type = arg(7), px = arg(8);
        bool usual = fmt == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5 && !x && !y && w > 0 && h > 0 && w <= 512 && h <= 512;
        if (!usual && !warned_sub) {
            warned_sub = true;
            olog("WARN glTexSubImage2D with unusual arguments %d %d %d %d 0x%x 0x%x", x, y, w, h, fmt, type);
        }
        const void *p = usual ? P(px, (uint32_t)(w * h * 2)) : ptr(px);
        if (usual && hash_log) olog("frame hash %016llx %dx%d", (unsigned long long)fnv((const uint8_t *)p, (size_t)w * h * 2), w, h);
        glTexSubImage2D(a0, (GLint)a1, x, y, w, h, fmt, type, p);
        return;
    }
    case 19: glVertexPointer((GLint)a0, a1, (GLsizei)a2, ptr(a3)); return;
    case 20: glViewport((GLint)a0, (GLint)a1, (GLsizei)a2, (GLsizei)a3); return;
    case 21: glColorPointer((GLint)a0, a1, (GLsizei)a2, ptr(a3)); return;
    case 22: {  // glGenTextures(n, ids): the real names go into guest memory
        int32_t n = (int32_t)a0;
        if (n <= 0) return;
        if (n > 256) die("glGenTextures of %d names", n);
        GLuint ids[256]; glGenTextures(n, ids);
        for (int32_t k = 0; k < n; k++) W32(a1 + 4u * (uint32_t)k, ids[k]);
        return;
    }
    default: die("GL import %u has no forward", i);
    }
}

void gl_real_init(bool debuggable) {
    char v[PROP_VALUE_MAX] = {0};
    if (debuggable && __system_property_get("debug.arelemu.hash", v) > 0 && !strcmp(v, "1")) hash_log = true;
    g_gl_hook = gl_call;
}

}  // namespace emu
