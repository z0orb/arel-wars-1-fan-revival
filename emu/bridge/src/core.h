// Core interface (PLAN.md DES-17). The bridge talks to the CPU core only through this file,
// so a change of core does not change the shims, the JNI code or the replacements.
#pragma once
#include <cstdint>

namespace emu {

// Callbacks from the core into the bridge.
struct CoreHooks {
    virtual ~CoreHooks() = default;
    // The guest ran "svc #n". Arguments are in the registers; write results to r0 (and r1).
    virtual void svc(uint32_t n) = 0;
    // The core cannot continue (undefined instruction, unmapped access). Must not return.
    [[noreturn]] virtual void fault(uint32_t pc, const char *what) = 0;
};

class Core {
public:
    virtual ~Core() = default;
    // base: host address of guest address 0 (a 4 GiB block). Readable ranges are checked by valid().
    static Core *create_dynarmic(uint8_t *base, CoreHooks *hooks, bool (*valid)(uint32_t addr, uint32_t len));
    virtual uint32_t &reg(int i) = 0;        // r0 to r15
    virtual uint32_t cpsr() const = 0;
    virtual void set_cpsr(uint32_t v) = 0;   // bit 5 (0x20) is the Thumb bit
    virtual void run() = 0;                  // run until stop() is called from a callback
    virtual void stop() = 0;
    virtual void step() = 0;                 // debug only
    virtual void clear_cache() = 0;          // after a hook change, debug only
    virtual bool executing() const = 0;
    // Debug and profile: before the guest instruction at addr (Thumb bit in bit 0) runs, call svc(n).
    // It changes no guest code. Set all hooks before the first run.
    virtual void hook_entry(uint32_t addr, uint32_t n) = 0;
    // Native replacement (DES-12): when the guest enters addr (Thumb bit in bit 0), call svc(n) in place of
    // the function, then return to lr. The handler reads the arguments and writes r0. It changes no guest code.
    virtual void hook_replace(uint32_t addr, uint32_t n) = 0;
};

}  // namespace emu
