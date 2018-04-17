// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/debugger.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <err.h>
#include <kernel/thread.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#define SYSCALL_OFFSETS_EQUAL(reg)                \
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

static void x86_fill_in_gregs_from_syscall(zx_thread_state_general_regs_t* out,
                                           const x86_syscall_general_regs_t* in) {
    memcpy(out, in, sizeof(*in));
}

static void x86_fill_in_syscall_from_gregs(x86_syscall_general_regs_t* out,
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

static void x86_fill_in_gregs_from_iframe(zx_thread_state_general_regs_t* out,
                                          const x86_iframe_t* in) {
    COPY_COMMON_IFRAME_REGS(out, in);
    out->rsp = in->user_sp;
    out->rip = in->ip;
    out->rflags = in->flags;
}

static void x86_fill_in_iframe_from_gregs(x86_iframe_t* out,
                                          const zx_thread_state_general_regs_t* in) {
    COPY_COMMON_IFRAME_REGS(out, in);
    out->user_sp = in->rsp;
    out->ip = in->rip;
    // Don't allow overriding privileged fields of rflags, and ignore writes
    // to reserved fields.
    out->flags &= ~X86_FLAGS_USER;
    out->flags |= in->rflags & X86_FLAGS_USER;
}

zx_status_t arch_get_general_regs(struct thread* thread, zx_thread_state_general_regs_t* out) {
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
