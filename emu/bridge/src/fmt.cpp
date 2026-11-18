// printf formatter on guest arguments (DES-05). Parses each conversion, takes its argument from
// the guest registers, the guest stack or a guest va_list (AAPCS: 8-byte values are 8-aligned),
// and formats it with the host snprintf. The host and guest are both bionic.
#include <string>
#include "guest.h"

namespace emu {

uint32_t ArgCursor::word() {
    if (ap) { uint32_t v = R32(ap); ap += 4; return v; }
    if (reg < 4) return g_core->reg(reg++);
    if (!sp) sp = g_core->reg(13);
    uint32_t v = R32(sp); sp += 4; return v;
}
uint64_t ArgCursor::dword() {
    if (ap) { ap = (ap + 7) & ~7u; uint64_t v = R32(ap) | (uint64_t)R32(ap + 4) << 32; ap += 8; return v; }
    if (reg < 4) {
        if (reg & 1) reg++;
        if (reg <= 2) { uint64_t v = g_core->reg(reg) | (uint64_t)g_core->reg(reg + 1) << 32; reg += 2; return v; }
        reg = 4;
    }
    if (!sp) sp = g_core->reg(13);
    sp = (sp + 7) & ~7u;
    uint64_t v = R32(sp) | (uint64_t)R32(sp + 4) << 32; sp += 8; return v;
}

int gformat(char *out, size_t cap, const char *fmt, ArgCursor &a) {
    std::string r;
    char tmp[512], spec[64];
    for (const char *p = fmt; *p;) {
        if (*p != '%') { r += *p++; continue; }
        const char *st = p++;
        if (*p == '%') { r += '%'; p++; continue; }
        std::string s = "%";
        while (*p && strchr("-+ #0'", *p)) s += *p++;
        if (*p == '*') { s += std::to_string((int32_t)a.word()); p++; }
        else while (*p >= '0' && *p <= '9') s += *p++;
        if (*p == '.') {
            s += *p++;
            if (*p == '*') { s += std::to_string((int32_t)a.word()); p++; }
            else while (*p >= '0' && *p <= '9') s += *p++;
        }
        int lng = 0;  // 0 int, 1 h, 2 hh, 3 l, 4 ll, 5 L, 6 j/z/t (32-bit)
        if (*p == 'h') { p++; lng = 1; if (*p == 'h') { p++; lng = 2; } }
        else if (*p == 'l') { p++; lng = 3; if (*p == 'l') { p++; lng = 4; } }
        else if (*p == 'L' || *p == 'q') { p++; lng = 5; }
        else if (*p == 'j' || *p == 'z' || *p == 't') { p++; lng = 6; }
        char c = *p;
        if (!c) die("printf format ends inside a conversion: %s", fmt);
        p++;
        if ((size_t)(p - st) >= sizeof spec - 4) die("printf conversion too long: %s", fmt);
        switch (c) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            const char *len = (lng == 1) ? "h" : (lng == 2) ? "hh" : (lng == 4 || lng == 5) ? "ll" : "";
            snprintf(spec, sizeof spec, "%s%s%c", s.c_str(), len, c);
            if (lng == 4 || lng == 5) snprintf(tmp, sizeof tmp, spec, (long long)a.dword());
            else {
                uint32_t v = a.word();
                if (c == 'd' || c == 'i') snprintf(tmp, sizeof tmp, spec, (int)(int32_t)v);
                else snprintf(tmp, sizeof tmp, spec, (unsigned)v);
            }
            r += tmp; break;
        }
        case 'c': snprintf(spec, sizeof spec, "%sc", s.c_str()); snprintf(tmp, sizeof tmp, spec, (int)a.word()); r += tmp; break;
        case 's': {
            uint32_t g = a.word();
            const char *str = g ? S(g) : "(null)";
            snprintf(spec, sizeof spec, "%ss", s.c_str());
            int n = snprintf(nullptr, 0, spec, str);
            std::string b((size_t)n + 1, '\0'); snprintf(&b[0], b.size(), spec, str); b.resize((size_t)n);
            r += b; break;
        }
        case 'p': snprintf(tmp, sizeof tmp, "0x%x", a.word()); r += tmp; break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            uint64_t b = a.dword(); double d; memcpy(&d, &b, 8);
            snprintf(spec, sizeof spec, "%s%c", s.c_str(), c);
            snprintf(tmp, sizeof tmp, spec, d); r += tmp; break;
        }
        case 'n': die("printf %%n in a guest format: %s", fmt);
        default: die("printf conversion %%%c not handled: %s", c, fmt);
        }
    }
    size_t n = r.size();
    if (cap) { size_t k = n < cap - 1 ? n : cap - 1; memcpy(out, r.data(), k); out[k] = 0; }
    return (int)n;
}

}  // namespace emu
