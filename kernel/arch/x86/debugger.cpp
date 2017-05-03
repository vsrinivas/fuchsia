// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <string.h>
#include <sys/types.h>
#include <kernel/thread.h>
#include <arch/x86.h>
#include <arch/debugger.h>
#include <magenta/syscalls/debug.h>

uint arch_num_regsets(void)
{
    // TODO(dje): for now. general regs
    return 1;
}

static status_t arch_get_general_regs(struct thread *thread, void *grp, uint32_t *buf_size)
{
    mx_x86_64_general_regs_t *out = (mx_x86_64_general_regs_t *)grp;

    uint32_t provided_buf_size = *buf_size;
    *buf_size = sizeof(*out);

    // Do "buffer too small" checks first. No point in prohibiting the caller
    // from finding out the needed size just because the thread is currently
    // running.
    if (provided_buf_size < sizeof(*out))
        return ERR_BUFFER_TOO_SMALL;

    if (!thread_stopped_in_exception(thread))
        return ERR_BAD_STATE;

    x86_iframe_t *in = thread->exception_context->frame;

    // TODO: We could get called while processing a synthetic exception where
    // there is no frame.
    if (in == NULL)
        return ERR_NOT_SUPPORTED;

    out->rax = in->rax;
    out->rbx = in->rbx;
    out->rcx = in->rcx;
    out->rdx = in->rdx;
    out->rsi = in->rsi;
    out->rdi = in->rdi;
    out->rbp = in->rbp;
    out->rsp = in->user_sp;
    out->r8 = in->r8;
    out->r9 = in->r9;
    out->r10 = in->r10;
    out->r11 = in->r11;
    out->r12 = in->r12;
    out->r13 = in->r13;
    out->r14 = in->r14;
    out->r15 = in->r15;
    out->rip = in->ip;
    out->rflags = in->flags;

    return NO_ERROR;
}

static status_t arch_set_general_regs(struct thread *thread, const void *grp, uint32_t buf_size)
{
    const mx_x86_64_general_regs_t *in = (const mx_x86_64_general_regs_t *)grp;

    if (buf_size != sizeof(*in))
        return ERR_INVALID_ARGS;

    if (!thread_stopped_in_exception(thread))
        return ERR_BAD_STATE;

    x86_iframe_t *out = thread->exception_context->frame;

    // TODO: We could get called while processing a synthetic exception where
    // there is no frame.
    if (out == NULL)
        return ERR_NOT_SUPPORTED;

    out->rax = in->rax;
    out->rbx = in->rbx;
    out->rcx = in->rcx;
    out->rdx = in->rdx;
    out->rsi = in->rsi;
    out->rdi = in->rdi;
    out->rbp = in->rbp;
    out->user_sp = in->rsp;
    out->r8 = in->r8;
    out->r9 = in->r9;
    out->r10 = in->r10;
    out->r11 = in->r11;
    out->r12 = in->r12;
    out->r13 = in->r13;
    out->r14 = in->r14;
    out->r15 = in->r15;
    out->ip = in->rip;
    out->flags = in->rflags;
    out->flags &= ~X86_FLAGS_RESERVED;

    return NO_ERROR;
}

status_t arch_get_regset(struct thread *thread, uint regset, void *regs, uint32_t *buf_size)
{
    switch (regset)
    {
    case 0:
        return arch_get_general_regs(thread, regs, buf_size);
    default:
        return ERR_INVALID_ARGS;
    }
}

status_t arch_set_regset(struct thread *thread, uint regset, const void *regs, uint32_t buf_size, bool priv)
{
    switch (regset)
    {
    case 0:
        return arch_set_general_regs(thread, regs, buf_size);
    default:
        return ERR_INVALID_ARGS;
    }
}
