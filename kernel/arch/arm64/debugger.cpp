// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <string.h>
#include <sys/types.h>
#include <arch/arm64.h>
#include <arch/debugger.h>
#include <kernel/thread.h>
#include <magenta/syscalls/debug.h>

// Only the NZCV flags (bits 31 to 28 respectively) of the CPSR are
// readable and writable by userland on ARM64.
static uint32_t kUserVisibleFlags = 0xf0000000;

uint arch_num_regsets(void)
{
    return 1; // TODO(dje): Just the general regs for now.
}

static status_t arch_get_general_regs(struct thread *thread, mx_arm64_general_regs_t *out, uint32_t *buf_size)
{
    uint32_t provided_buf_size = *buf_size;
    *buf_size = sizeof(*out);

    if (provided_buf_size < sizeof(*out))
        return MX_ERR_BUFFER_TOO_SMALL;

    if (thread_stopped_in_exception(thread)) {
        // TODO(dje): We could get called while processing a synthetic
        // exception where there is no frame.
        if (thread->exception_context->frame == NULL)
            return MX_ERR_NOT_SUPPORTED;
    } else {
        // TODO(dje): Punt if, for example, suspended in channel call.
        // Can be removed when MG-747 done.
        if (thread->arch.suspended_general_regs == nullptr)
            return MX_ERR_NOT_SUPPORTED;
    }

    struct arm64_iframe_long *in = thread->arch.suspended_general_regs;
    DEBUG_ASSERT(in);

    static_assert(sizeof(in->r) == sizeof(out->r), "");
    memcpy(&out->r[0], &in->r[0], sizeof(in->r));
    out->lr = in->lr;
    out->sp = in->usp;
    out->pc = in->elr;
    out->cpsr = in->spsr & kUserVisibleFlags;

    return MX_OK;
}

static status_t arch_set_general_regs(struct thread *thread, const mx_arm64_general_regs_t *in, uint32_t buf_size)
{
    if (buf_size != sizeof(*in))
        return MX_ERR_INVALID_ARGS;

    if (thread_stopped_in_exception(thread)) {
        // TODO(dje): We could get called while processing a synthetic
        // exception where there is no frame.
        if (thread->exception_context->frame == NULL)
            return MX_ERR_NOT_SUPPORTED;
    } else {
        // TODO(dje): Punt if, for example, suspended in channel call.
        // Can be removed when MG-747 done.
        if (thread->arch.suspended_general_regs == nullptr)
            return MX_ERR_NOT_SUPPORTED;
    }

    struct arm64_iframe_long *out = thread->arch.suspended_general_regs;
    DEBUG_ASSERT(out);

    static_assert(sizeof(out->r) == sizeof(in->r), "");
    memcpy(&out->r[0], &in->r[0], sizeof(in->r));
    out->lr = in->lr;
    out->usp = in->sp;
    out->elr = in->pc;
    out->spsr = (out->spsr & ~kUserVisibleFlags)
        | (in->cpsr & kUserVisibleFlags);

    return MX_OK;
}

// The caller is responsible for making sure the thread is in an exception
// or is suspended, and stays so.
status_t arch_get_regset(struct thread *thread, uint regset, void *regs, uint32_t *buf_size)
{
    switch (regset)
    {
    case 0:
        return arch_get_general_regs(thread, (mx_arm64_general_regs_t *)regs, buf_size);
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

// The caller is responsible for making sure the thread is in an exception
// or is suspended, and stays so.
status_t arch_set_regset(struct thread *thread, uint regset, const void *regs, uint32_t buf_size)
{
    switch (regset)
    {
    case 0:
        return arch_set_general_regs(thread, (mx_arm64_general_regs_t *)regs, buf_size);
    default:
        return MX_ERR_INVALID_ARGS;
    }
}
