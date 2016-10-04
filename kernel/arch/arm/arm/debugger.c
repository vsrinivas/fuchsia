// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <sys/types.h>
#include <kernel/thread.h>
#include <arch/arm.h>
#include <arch/debugger.h>

uint arch_num_regsets(void)
{
    return 1; // TODO(dje): Just the general regs for now.
}

static status_t arch_get_general_regs(struct thread *thread, struct arch_gen_regs *gr, uint32_t *buf_size)
{
    uint32_t provided_buf_size = *buf_size;
    *buf_size = sizeof(*gr);

    if (provided_buf_size < sizeof(*gr))
        return ERR_BUFFER_TOO_SMALL;

    if ((thread->flags & THREAD_FLAG_STOPPED_FOR_EXCEPTION) == 0)
        return ERR_BAD_STATE;

    return ERR_NOT_SUPPORTED;
}

static status_t arch_set_general_regs(struct thread *thread, const struct arch_gen_regs *gr, uint32_t buf_size)
{
    if (buf_size != sizeof(*gr))
        return ERR_INVALID_ARGS;

    if ((thread->flags & THREAD_FLAG_STOPPED_FOR_EXCEPTION) == 0)
        return ERR_BAD_STATE;

    return ERR_NOT_SUPPORTED;
}

status_t arch_get_regset(struct thread *thread, uint regset, void *regs, uint32_t* buf_size)
{
    switch (regset)
    {
    case 0:
        return arch_get_general_regs(thread, regs, buf_size);
    default:
        return ERR_INVALID_ARGS;
    }
}

status_t arch_set_regset(struct thread *thread, uint regset, const void *regs, size_t buf_size, bool priv)
{
    switch (regset)
    {
    case 0:
        return arch_set_general_regs(thread, regs, buf_size);
    default:
        return ERR_INVALID_ARGS;
    }
}
