// dynarmic A32 behind the core interface (DES-01, DES-17).
// Fastmem on the whole 4 GiB guest block. A fastmem fault falls back to the callbacks below,
// which check the range and report a crash for an unmapped address.
#include <cstring>
#include <unordered_map>
#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <dynarmic/frontend/A32/a32_types.h>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include "core.h"

namespace emu {
namespace {

class DynCore final : public Core, public Dynarmic::A32::UserCallbacks {
public:
    DynCore(uint8_t *base, CoreHooks *hooks, bool (*valid)(uint32_t, uint32_t)) : base_(base), hooks_(hooks), valid_(valid) {
        Dynarmic::A32::UserConfig c;
        c.callbacks = this;
        // The reference runs on ARMv8 in AArch32 state, so use v8 rules (unaligned loads, interworking).
        c.arch_version = Dynarmic::A32::ArchVersion::v8;
        c.fastmem_pointer = reinterpret_cast<uintptr_t>(base);
        c.recompile_on_fastmem_failure = true;
        c.enable_cycle_counting = false;
        c.code_cache_size = 64 * 1024 * 1024;
        jit_ = new Dynarmic::A32::Jit(c);
    }
    ~DynCore() override { delete jit_; }

    uint32_t &reg(int i) override { return jit_->Regs()[i]; }
    uint32_t cpsr() const override { return jit_->Cpsr(); }
    void set_cpsr(uint32_t v) override { jit_->SetCpsr(v); }
    void run() override { jit_->ClearHalt(); jit_->Run(); }
    void stop() override { jit_->HaltExecution(); }
    void step() override { jit_->Step(); }
    void clear_cache() override { jit_->ClearCache(); }
    bool executing() const override { return jit_->IsExecuting(); }
    void hook_entry(uint32_t addr, uint32_t n) override { entry_hooks_[addr] = n; }
    void hook_replace(uint32_t addr, uint32_t n) override { replace_hooks_[addr] = n; }
    bool PreCodeReadHook(bool is_thumb, uint32_t pc, Dynarmic::A32::IREmitter &ir) override {
        if (!replace_hooks_.empty()) {
            // A replaced function (DES-12): call the handler, then "bx lr". The guest code is not read.
            auto it = replace_hooks_.find(pc | (is_thumb ? 1u : 0u));
            if (it != replace_hooks_.end()) {
                ir.CallSupervisor(ir.Imm32(it->second));
                ir.BXWritePC(ir.GetRegister(Dynarmic::A32::Reg::LR));
                ir.SetTerm(Dynarmic::IR::Term::CheckHalt{Dynarmic::IR::Term::PopRSBHint{}});
                return false;
            }
        }
        if (!entry_hooks_.empty()) {
            auto it = entry_hooks_.find(pc | (is_thumb ? 1u : 0u));
            if (it != entry_hooks_.end()) ir.CallSupervisor(ir.Imm32(it->second));
        }
        return true;
    }

    // ---- memory callbacks (slow path) ----
    template <typename T> T rd(uint32_t a) {
        if (!valid_(a, sizeof(T))) bad(a, "read");
        T v; std::memcpy(&v, base_ + a, sizeof v); return v;
    }
    template <typename T> void wr(uint32_t a, T v) {
        if (!valid_(a, sizeof(T))) bad(a, "write");
        std::memcpy(base_ + a, &v, sizeof v);
    }
    [[noreturn]] void bad(uint32_t a, const char *what) {
        static char msg[96];
        snprintf(msg, sizeof msg, "guest %s of unmapped address 0x%08x", what, a);
        hooks_->fault(jit_->Regs()[15], msg);
    }
    std::optional<std::uint32_t> MemoryReadCode(uint32_t a) override {
        if (!valid_(a, 4)) return std::nullopt;
        uint32_t v; std::memcpy(&v, base_ + a, 4); return v;
    }
    uint8_t MemoryRead8(uint32_t a) override { return rd<uint8_t>(a); }
    uint16_t MemoryRead16(uint32_t a) override { return rd<uint16_t>(a); }
    uint32_t MemoryRead32(uint32_t a) override { return rd<uint32_t>(a); }
    uint64_t MemoryRead64(uint32_t a) override { return rd<uint64_t>(a); }
    void MemoryWrite8(uint32_t a, uint8_t v) override { wr(a, v); }
    void MemoryWrite16(uint32_t a, uint16_t v) override { wr(a, v); }
    void MemoryWrite32(uint32_t a, uint32_t v) override { wr(a, v); }
    void MemoryWrite64(uint32_t a, uint64_t v) override { wr(a, v); }

    void InterpreterFallback(uint32_t pc, size_t n) override {
        static char msg[96];
        snprintf(msg, sizeof msg, "core has no translation for %zu instruction(s)", n);
        hooks_->fault(pc, msg);
    }
    void CallSVC(uint32_t swi) override { hooks_->svc(swi); }
    void ExceptionRaised(uint32_t pc, Dynarmic::A32::Exception e) override {
        using E = Dynarmic::A32::Exception;
        switch (e) {
        case E::PreloadData: case E::PreloadDataWithIntentToWrite: case E::PreloadInstruction:
        case E::Yield: case E::SendEvent: case E::SendEventLocal: case E::WaitForEvent: case E::WaitForInterrupt:
            return;  // hints: no effect on a single-threaded guest
        default: break;
        }
        static char msg[64];
        snprintf(msg, sizeof msg, "core exception %d", static_cast<int>(e));
        hooks_->fault(pc, msg);
    }
    void AddTicks(uint64_t) override {}
    uint64_t GetTicksRemaining() override { return 1u << 30; }

private:
    uint8_t *base_;
    CoreHooks *hooks_;
    bool (*valid_)(uint32_t, uint32_t);
    Dynarmic::A32::Jit *jit_ = nullptr;
    std::unordered_map<uint32_t, uint32_t> entry_hooks_, replace_hooks_;
};

}  // namespace

Core *Core::create_dynarmic(uint8_t *base, CoreHooks *hooks, bool (*valid)(uint32_t, uint32_t)) {
    return new DynCore(base, hooks, valid);
}

}  // namespace emu
