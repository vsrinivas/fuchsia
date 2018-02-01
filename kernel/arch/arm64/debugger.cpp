// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64.h>
#include <arch/debugger.h>
#include <err.h>
#include <kernel/thread.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

// Only the NZCV flags (bits 31 to 28 respectively) of the CPSR are
// readable and writable by userland on ARM64.
static uint32_t kUserVisibleFlags = 0xf0000000;

zx_status_t arch_get_general_regs(struct thread* thread, zx_thread_state_general_regs_t* out) {
    if (thread_stopped_in_exception(thread)) {
        // TODO(dje): We could get called while processing a synthetic
        // exception where there is no frame.
        if (thread->exception_context->frame == NULL)
            return ZX_ERR_NOT_SUPPORTED;
    } else {
        // TODO(dje): Punt if, for example, suspended in channel call.
        // Can be removed when ZX-747 done.
        if (thread->arch.suspended_general_regs == nullptr)
            return ZX_ERR_NOT_SUPPORTED;
    }

    struct arm64_iframe_long* in = thread->arch.suspended_general_regs;
    DEBUG_ASSERT(in);

    static_assert(sizeof(in->r) == sizeof(out->r), "");
    memcpy(&out->r[0], &in->r[0], sizeof(in->r));
    out->lr = in->lr;
    out->sp = in->usp;
    out->pc = in->elr;
    out->cpsr = in->spsr & kUserVisibleFlags;

    return ZX_OK;
}

zx_status_t arch_set_general_regs(struct thread* thread, const zx_thread_state_general_regs_t* in) {
    if (thread_stopped_in_exception(thread)) {
        // TODO(dje): We could get called while processing a synthetic
        // exception where there is no frame.
        if (thread->exception_context->frame == NULL)
            return ZX_ERR_NOT_SUPPORTED;
    } else {
        // TODO(dje): Punt if, for example, suspended in channel call.
        // Can be removed when ZX-747 done.
        if (thread->arch.suspended_general_regs == nullptr)
            return ZX_ERR_NOT_SUPPORTED;
    }

    struct arm64_iframe_long* out = thread->arch.suspended_general_regs;
    DEBUG_ASSERT(out);

    static_assert(sizeof(out->r) == sizeof(in->r), "");
    memcpy(&out->r[0], &in->r[0], sizeof(in->r));
    out->lr = in->lr;
    out->usp = in->sp;
    out->elr = in->pc;
    out->spsr = (out->spsr & ~kUserVisibleFlags) | (in->cpsr & kUserVisibleFlags);

    return ZX_OK;
}
