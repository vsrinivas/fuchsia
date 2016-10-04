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
#include <magenta/syscalls-debug.h>

uint arch_num_regsets(void)
{
#if ARCH_X86_64
    // TODO(dje): for now. general regs
    return 1;
#else
    return 0;
#endif
}

static status_t arch_get_general_regs(struct thread *thread, void *grp, uint32_t *buf_size)
{
#if ARCH_X86_64
    mx_x86_64_general_regs_t *gr = grp;

    uint32_t provided_buf_size = *buf_size;
    *buf_size = sizeof(*gr);

    if (provided_buf_size < sizeof(*gr))
        return ERR_BUFFER_TOO_SMALL;

    if ((thread->flags & THREAD_FLAG_STOPPED_FOR_EXCEPTION) == 0)
        return ERR_BAD_STATE;

    x86_iframe_t *p = thread->exception_context->frame;
    gr->rax = p->rax;
    gr->rbx = p->rbx;
    gr->rcx = p->rcx;
    gr->rdx = p->rdx;
    gr->rsi = p->rsi;
    gr->rdi = p->rdi;
    gr->rbp = p->rbp;
    gr->rsp = p->user_sp;
    gr->r8 = p->r8;
    gr->r9 = p->r9;
    gr->r10 = p->r10;
    gr->r11 = p->r11;
    gr->r12 = p->r12;
    gr->r13 = p->r13;
    gr->r14 = p->r14;
    gr->r15 = p->r15;
    gr->rip = p->ip;
    gr->rflags = p->flags;

    return NO_ERROR;
#else
    return ERR_NOT_SUPPORTED;
#endif
}

static status_t arch_set_general_regs(struct thread *thread, const void *grp, uint32_t buf_size)
{
#if ARCH_X86_64
    const mx_x86_64_general_regs_t *gr = grp;

    if (buf_size != sizeof(*gr))
        return ERR_INVALID_ARGS;

    if ((thread->flags & THREAD_FLAG_STOPPED_FOR_EXCEPTION) == 0)
        return ERR_BAD_STATE;

    x86_iframe_t *p = thread->exception_context->frame;
    p->rax = gr->rax;
    p->rbx = gr->rbx;
    p->rcx = gr->rcx;
    p->rdx = gr->rdx;
    p->rsi = gr->rsi;
    p->rdi = gr->rdi;
    p->rbp = gr->rbp;
    p->user_sp = gr->rsp;
    p->r8 = gr->r8;
    p->r9 = gr->r9;
    p->r10 = gr->r10;
    p->r11 = gr->r11;
    p->r12 = gr->r12;
    p->r13 = gr->r13;
    p->r14 = gr->r14;
    p->r15 = gr->r15;
    p->ip = gr->rip;
    p->flags = gr->rflags;
    p->flags &= ~X86_FLAGS_RESERVED;

    return NO_ERROR;
#else
    return ERR_NOT_SUPPORTED;
#endif
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
