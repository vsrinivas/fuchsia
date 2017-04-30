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

#define SYSCALL_OFFSETS_EQUAL(reg) \
  (__offsetof(mx_x86_64_general_regs_t, reg) == \
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
static_assert(sizeof(mx_x86_64_general_regs_t) == sizeof(x86_syscall_general_regs_t), "");

static void x86_fill_in_gregs_from_syscall(mx_x86_64_general_regs_t *out,
                                           const x86_syscall_general_regs_t *in)
{
    memcpy(out, in, sizeof(*in));
}

static void x86_fill_in_syscall_from_gregs(x86_syscall_general_regs_t *out,
                                           const mx_x86_64_general_regs_t *in)
{
    // Don't allow overriding privileged fields of rflags, and ignore writes
    // to reserved fields.
    const uint64_t orig_rflags = out->rflags;
    memcpy(out, in, sizeof(*in));
    out->rflags = orig_rflags & ~X86_FLAGS_USER;
    out->rflags |= in->rflags & X86_FLAGS_USER;
}

#define COPY_REG(out, in, reg) (out)->reg = (in)->reg
#define COPY_COMMON_IFRAME_REGS(out, in) \
  do { \
    COPY_REG(out, in, rax); \
    COPY_REG(out, in, rbx); \
    COPY_REG(out, in, rcx); \
    COPY_REG(out, in, rdx); \
    COPY_REG(out, in, rsi); \
    COPY_REG(out, in, rdi); \
    COPY_REG(out, in, rbp); \
    COPY_REG(out, in, r8); \
    COPY_REG(out, in, r9); \
    COPY_REG(out, in, r10); \
    COPY_REG(out, in, r11); \
    COPY_REG(out, in, r12); \
    COPY_REG(out, in, r13); \
    COPY_REG(out, in, r14); \
    COPY_REG(out, in, r15); \
  } while (0)

static void x86_fill_in_gregs_from_iframe(mx_x86_64_general_regs_t *out,
                                          const x86_iframe_t *in)
{
    COPY_COMMON_IFRAME_REGS(out, in);
    out->rsp = in->user_sp;
    out->rip = in->ip;
    out->rflags = in->flags;
}

static void x86_fill_in_iframe_from_gregs(x86_iframe_t *out,
                                          const mx_x86_64_general_regs_t *in)
{
    COPY_COMMON_IFRAME_REGS(out, in);
    out->user_sp = in->rsp;
    out->ip = in->rip;
    // Don't allow overriding privileged fields of rflags, and ignore writes
    // to reserved fields.
    out->flags &= ~X86_FLAGS_USER;
    out->flags |= in->rflags & X86_FLAGS_USER;
}

static status_t arch_get_general_regs(struct thread *thread, void *grp, uint32_t *buf_size)
{
    mx_x86_64_general_regs_t *out = (mx_x86_64_general_regs_t *)grp;

    uint32_t provided_buf_size = *buf_size;
    *buf_size = sizeof(*out);

    if (provided_buf_size < sizeof(*out))
        return ERR_BUFFER_TOO_SMALL;

    if (thread_stopped_in_exception(thread)) {
        // TODO(dje): We could get called while processing a synthetic
        // exception where there is no frame.
        if (thread->exception_context->frame == NULL)
            return ERR_NOT_SUPPORTED;
    }

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
            return ERR_BAD_STATE;
    }

    return NO_ERROR;
}

static status_t arch_set_general_regs(struct thread *thread, const void *grp, uint32_t buf_size)
{
    const mx_x86_64_general_regs_t *in = (const mx_x86_64_general_regs_t *)grp;

    if (buf_size != sizeof(*in))
        return ERR_INVALID_ARGS;

    if (thread_stopped_in_exception(thread)) {
        // TODO(dje): We could get called while processing a synthetic
        // exception where there is no frame.
        if (thread->exception_context->frame == NULL)
            return ERR_NOT_SUPPORTED;
    }

    DEBUG_ASSERT(thread->arch.suspended_general_regs.gregs);
    switch (thread->arch.general_regs_source) {
        case X86_GENERAL_REGS_SYSCALL:
            x86_fill_in_syscall_from_gregs(thread->arch.suspended_general_regs.syscall, in);
            break;
        case X86_GENERAL_REGS_IFRAME:
            x86_fill_in_iframe_from_gregs(thread->arch.suspended_general_regs.iframe, in);
            break;
        default:
            DEBUG_ASSERT(false);
            return ERR_BAD_STATE;
    }

    return NO_ERROR;
}

// The caller is responsible for making sure the thread is in an exception
// or is suspended, and stays so.
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

// The caller is responsible for making sure the thread is in an exception
// or is suspended, and stays so.
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
