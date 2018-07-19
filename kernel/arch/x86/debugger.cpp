// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/debugger.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/registers.h>
#include <err.h>
#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

// Note on locking: The below functions need to read and write the register state and make sure that
// nothing happens with respect to scheduling that thread while this is happening. As a result they
// use AutoThreadLock. In most cases this will not be necessary but there are relatively few
// guarantees so we lock the scheduler. Since these functions are used mostly for debugging, this
// shouldn't be too significant a performance penalty.

namespace {

#define SYSCALL_OFFSETS_EQUAL(reg)                      \
    (__offsetof(zx_thread_state_general_regs_t, reg) == \
     __offsetof(x86_syscall_general_regs_t, reg))

static_assert(SYSCALL_OFFSETS_EQUAL(rax), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rbx), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rcx), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rdx), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rsi), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rdi), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rbp), "");
static_assert(SYSCALL_OFFSETS_EQUAL(rsp), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r8), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r9), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r10), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r11), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r12), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r13), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r14), "");
static_assert(SYSCALL_OFFSETS_EQUAL(r15), "");
static_assert(sizeof(zx_thread_state_general_regs_t) == sizeof(x86_syscall_general_regs_t), "");

void x86_fill_in_gregs_from_syscall(zx_thread_state_general_regs_t* out,
                                    const x86_syscall_general_regs_t* in) {
    memcpy(out, in, sizeof(*in));
}

void x86_fill_in_syscall_from_gregs(x86_syscall_general_regs_t* out,
                                    const zx_thread_state_general_regs_t* in) {
    // Don't allow overriding privileged fields of rflags, and ignore writes
    // to reserved fields.
    const uint64_t orig_rflags = out->rflags;
    memcpy(out, in, sizeof(*in));
    out->rflags = orig_rflags & ~X86_FLAGS_USER;
    out->rflags |= in->rflags & X86_FLAGS_USER;
}

#define COPY_REG(out, in, reg) (out)->reg = (in)->reg
#define COPY_COMMON_IFRAME_REGS(out, in) \
    do {                                 \
        COPY_REG(out, in, rax);          \
        COPY_REG(out, in, rbx);          \
        COPY_REG(out, in, rcx);          \
        COPY_REG(out, in, rdx);          \
        COPY_REG(out, in, rsi);          \
        COPY_REG(out, in, rdi);          \
        COPY_REG(out, in, rbp);          \
        COPY_REG(out, in, r8);           \
        COPY_REG(out, in, r9);           \
        COPY_REG(out, in, r10);          \
        COPY_REG(out, in, r11);          \
        COPY_REG(out, in, r12);          \
        COPY_REG(out, in, r13);          \
        COPY_REG(out, in, r14);          \
        COPY_REG(out, in, r15);          \
    } while (0)

void x86_fill_in_gregs_from_iframe(zx_thread_state_general_regs_t* out,
                                   const x86_iframe_t* in) {
    COPY_COMMON_IFRAME_REGS(out, in);
    out->rsp = in->user_sp;
    out->rip = in->ip;
    out->rflags = in->flags;
}

void x86_fill_in_iframe_from_gregs(x86_iframe_t* out,
                                   const zx_thread_state_general_regs_t* in) {
    COPY_COMMON_IFRAME_REGS(out, in);
    out->user_sp = in->rsp;
    out->ip = in->rip;
    // Don't allow overriding privileged fields of rflags, and ignore writes
    // to reserved fields.
    out->flags &= ~X86_FLAGS_USER;
    out->flags |= in->rflags & X86_FLAGS_USER;
}

// Whether an operation gets thread state or sets it.
enum class RegAccess { kGet,
                       kSet };

// Backend for arch_get_vector_regs and arch_set_vector_regs. This does a read or write of the
// thread to or from the regs structure.
zx_status_t x86_get_set_vector_regs(struct thread* thread, zx_thread_state_vector_regs* regs,
                                    RegAccess access) {
    // Function to copy memory in the correct direction. Write the code using this function as if it
    // was "memcpy" in "get" mode, and it will be reversed in "set" mode.
    auto get_set_memcpy = (access == RegAccess::kGet) ? [](void* regs, void* thread, size_t size) { memcpy(regs, thread, size); } : // Get mode.
                              [](void* regs, void* thread, size_t size) { memcpy(thread, regs, size); };                            // Set mode.

    if (access == RegAccess::kGet) {
        // Not all parts will be filled in in all cases so zero out first.
        memset(regs, 0, sizeof(zx_thread_state_vector_regs));
    }

    // Whether to force the components to be marked present in the xsave area.
    bool mark_present = access == RegAccess::kSet;

    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    constexpr int kNumSSERegs = 16;

    // The low 128 bits of registers 0-15 come from the legacy area and are always present.
    constexpr int kXmmRegSize = 16; // Each XMM register is 128 bits / 16 bytes.
    uint32_t comp_size = 0;
    x86_xsave_legacy_area* save = static_cast<x86_xsave_legacy_area*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_SSE, mark_present,
                                                  &comp_size));
    DEBUG_ASSERT(save); // Legacy getter should always succeed.
    for (int i = 0; i < kNumSSERegs; i++) {
        get_set_memcpy(&regs->zmm[i].v[0], &save->xmm[i], kXmmRegSize);
    }

    // MXCSR (always present): 32-bit status word.
    get_set_memcpy(&regs->mxcsr, &save->mxcsr, 4);

    // AVX grows the registers to 256 bits each. Optional.
    constexpr int kYmmHighSize = 16; // Additional bytes in each register.
    uint8_t* ymm_highbits = static_cast<uint8_t*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_AVX, mark_present,
                                                  &comp_size));
    if (ymm_highbits) {
        DEBUG_ASSERT(comp_size == kYmmHighSize * kNumSSERegs);
        for (int i = 0; i < kNumSSERegs; i++) {
            get_set_memcpy(&regs->zmm[i].v[2], &ymm_highbits[i * kYmmHighSize], kYmmHighSize);
        }
    }

    // AVX-512 opmask registers (8 64-bit registers). Optional.
    constexpr int kNumOpmaskRegs = 8;
    uint64_t* opmask = static_cast<uint64_t*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_AVX512_OPMASK, mark_present,
                                                  &comp_size));
    if (opmask) {
        DEBUG_ASSERT(comp_size == kNumOpmaskRegs * sizeof(uint64_t));
        for (int i = 0; i < kNumOpmaskRegs; i++) {
            get_set_memcpy(&regs->opmask[i], &opmask[i], sizeof(uint64_t));
        }
    }

    // AVX-512 high bits (256 bits extra each) for ZMM0-15.
    constexpr int kZmmHighSize = 32; // Additional bytes in each register.
    uint8_t* zmm_highbits = static_cast<uint8_t*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_AVX512_LOWERZMM_HIGH,
                                                  mark_present, &comp_size));
    if (zmm_highbits) {
        DEBUG_ASSERT(comp_size == kZmmHighSize * kNumSSERegs);
        for (int i = 0; i < kNumSSERegs; i++) {
            get_set_memcpy(&regs->zmm[i].v[4], &zmm_highbits[i * kZmmHighSize], kZmmHighSize);
        }
    }

    // AVX-512 registers 16-31 (512 bits each) are in component 7.
    constexpr int kNumZmmHighRegs = 16; // Extra registers added over xmm/ymm.
    constexpr int kZmmRegSize = 64;     // Total register size.
    uint8_t* zmm_highregs = static_cast<uint8_t*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_AVX512_HIGHERZMM,
                                                  mark_present, &comp_size));
    if (zmm_highregs) {
        DEBUG_ASSERT(comp_size == kNumZmmHighRegs * kZmmRegSize);
        for (int i = 0; i < kNumZmmHighRegs; i++) {
            get_set_memcpy(&regs->zmm[i + kNumSSERegs], &zmm_highregs[i * kZmmRegSize],
                           kZmmRegSize);
        }
    }

    return ZX_OK;
}

} // namespace

zx_status_t arch_get_general_regs(struct thread* thread, zx_thread_state_general_regs_t* out) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

    // Punt if registers aren't available. E.g.,
    // ZX-563 (registers aren't available in synthetic exceptions)
    if (thread->arch.suspended_general_regs.gregs == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(thread->arch.suspended_general_regs.gregs);
    switch (thread->arch.general_regs_source) {
    case X86_GENERAL_REGS_SYSCALL:
        x86_fill_in_gregs_from_syscall(out, thread->arch.suspended_general_regs.syscall);
        break;
    case X86_GENERAL_REGS_IFRAME:
        x86_fill_in_gregs_from_iframe(out, thread->arch.suspended_general_regs.iframe);
        break;
    default:
        DEBUG_ASSERT(false);
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}

zx_status_t arch_set_general_regs(struct thread* thread, const zx_thread_state_general_regs_t* in) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

    // Punt if registers aren't available. E.g.,
    // ZX-563 (registers aren't available in synthetic exceptions)
    if (thread->arch.suspended_general_regs.gregs == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(thread->arch.suspended_general_regs.gregs);
    switch (thread->arch.general_regs_source) {
    case X86_GENERAL_REGS_SYSCALL: {
        // Disallow setting RIP to a non-canonical address, to prevent
        // returning to such addresses using the SYSRET instruction.
        // See docs/sysret_problem.md.  Note that this check also
        // disallows canonical top-bit-set addresses, but allowing such
        // addresses is not useful and it is simpler to disallow them.
        uint8_t addr_width = x86_linear_address_width();
        uint64_t noncanonical_addr = ((uint64_t)1) << (addr_width - 1);
        if (in->rip >= noncanonical_addr)
            return ZX_ERR_INVALID_ARGS;
        x86_fill_in_syscall_from_gregs(thread->arch.suspended_general_regs.syscall, in);
        break;
    }
    case X86_GENERAL_REGS_IFRAME:
        x86_fill_in_iframe_from_gregs(thread->arch.suspended_general_regs.iframe, in);
        break;
    default:
        DEBUG_ASSERT(false);
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}

zx_status_t arch_get_single_step(struct thread* thread, bool* single_step) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

    // Punt if registers aren't available. E.g.,
    // ZX-563 (registers aren't available in synthetic exceptions)
    if (thread->arch.suspended_general_regs.gregs == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    uint64_t* flags = nullptr;
    switch (thread->arch.general_regs_source) {
    case X86_GENERAL_REGS_SYSCALL:
        flags = &thread->arch.suspended_general_regs.syscall->rflags;
        break;
    case X86_GENERAL_REGS_IFRAME:
        flags = &thread->arch.suspended_general_regs.iframe->flags;
        break;
    default:
        DEBUG_ASSERT(false);
        return ZX_ERR_BAD_STATE;
    }

    *single_step = !!(*flags & X86_FLAGS_TF);
    return ZX_OK;
}

zx_status_t arch_set_single_step(struct thread* thread, bool single_step) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

    // Punt if registers aren't available. E.g.,
    // ZX-563 (registers aren't available in synthetic exceptions)
    if (thread->arch.suspended_general_regs.gregs == nullptr)
        return ZX_ERR_NOT_SUPPORTED;

    uint64_t* flags = nullptr;
    switch (thread->arch.general_regs_source) {
    case X86_GENERAL_REGS_SYSCALL:
        flags = &thread->arch.suspended_general_regs.syscall->rflags;
        break;
    case X86_GENERAL_REGS_IFRAME:
        flags = &thread->arch.suspended_general_regs.iframe->flags;
        break;
    default:
        DEBUG_ASSERT(false);
        return ZX_ERR_BAD_STATE;
    }

    if (single_step) {
        *flags |= X86_FLAGS_TF;
    } else {
        *flags &= ~X86_FLAGS_TF;
    }
    return ZX_OK;
}

zx_status_t arch_get_fp_regs(struct thread* thread, zx_thread_state_fp_regs* out) {
    // Don't leak any reserved fields.
    memset(out, 0, sizeof(zx_thread_state_fp_regs));

    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    uint32_t comp_size = 0;
    x86_xsave_legacy_area* save = static_cast<x86_xsave_legacy_area*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_X87, false, &comp_size));
    DEBUG_ASSERT(save); // Legacy getter should always succeed.

    out->fcw = save->fcw;
    out->fsw = save->fsw;
    out->ftw = save->ftw;
    out->fop = save->fop;
    out->fip = save->fip;
    out->fdp = save->fdp;
    memcpy(&out->st[0], &save->st[0], sizeof(out->st));

    return ZX_OK;
}

zx_status_t arch_set_fp_regs(struct thread* thread, const zx_thread_state_fp_regs* in) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    uint32_t comp_size = 0;
    x86_xsave_legacy_area* save = static_cast<x86_xsave_legacy_area*>(
        x86_get_extended_register_state_component(thread->arch.extended_register_state,
                                                  X86_XSAVE_STATE_INDEX_X87, true, &comp_size));
    DEBUG_ASSERT(save); // Legacy getter should always succeed.

    save->fcw = in->fcw;
    save->fsw = in->fsw;
    save->ftw = in->ftw;
    save->fop = in->fop;
    save->fip = in->fip;
    save->fdp = in->fdp;
    memcpy(&save->st[0], &in->st[0], sizeof(in->st));

    return ZX_OK;
}

zx_status_t arch_get_vector_regs(struct thread* thread, zx_thread_state_vector_regs* out) {
    return x86_get_set_vector_regs(thread, out, RegAccess::kGet);
}

zx_status_t arch_set_vector_regs(struct thread* thread, const zx_thread_state_vector_regs* in) {
    // The get_set function won't write in "kSet" mode so the const_cast is safe.
    return x86_get_set_vector_regs(thread, const_cast<zx_thread_state_vector_regs*>(in),
                                   RegAccess::kSet);
}

zx_status_t arch_get_x86_register_fs(struct thread* thread, uint64_t* out) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    *out = thread->arch.fs_base;
    return ZX_OK;
}

zx_status_t arch_set_x86_register_fs(struct thread* thread, const uint64_t* in) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    thread->arch.fs_base = *in;
    return ZX_OK;
}

zx_status_t arch_get_x86_register_gs(struct thread* thread, uint64_t* out) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    *out = thread->arch.gs_base;
    return ZX_OK;
}

zx_status_t arch_set_x86_register_gs(struct thread* thread, const uint64_t* in) {
    Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (thread->state == THREAD_RUNNING) {
        return ZX_ERR_BAD_STATE;
    }

    thread->arch.gs_base = *in;
    return ZX_OK;
}
