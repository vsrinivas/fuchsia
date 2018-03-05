// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <zircon/compiler.h>
#include <arch/x86/registers.h>
#include <sys/types.h>

__BEGIN_CDECLS

struct arch_thread {
    vaddr_t sp;
#if __has_feature(safe_stack)
    vaddr_t unsafe_sp;
#endif
    vaddr_t fs_base;
    vaddr_t gs_base;

    // Which entry of |suspended_general_regs| to use.
    // One of X86_GENERAL_REGS_*.
    uint32_t general_regs_source;

    // Debugger access to userspace general regs while suspended or stopped
    // in an exception.
    // The regs are saved on the stack and then a pointer is stored here.
    // NULL if not suspended or stopped in an exception.
    union {
        void *gregs;
        x86_syscall_general_regs_t *syscall;
        x86_iframe_t *iframe;
    } suspended_general_regs;

    /* buffer to save fpu and extended register (e.g., PT) state */
    vaddr_t *extended_register_state;
    uint8_t extended_register_buffer[X86_MAX_EXTENDED_REGISTER_SIZE + 64];

    /* if non-NULL, address to return to on page fault */
    void *page_fault_resume;
};

static inline void x86_set_suspended_general_regs(struct arch_thread *thread,
                                                  uint32_t source, void *gregs)
{
    DEBUG_ASSERT(thread->suspended_general_regs.gregs == NULL);
    DEBUG_ASSERT(gregs != NULL);
    DEBUG_ASSERT(source != X86_GENERAL_REGS_NONE);
    thread->general_regs_source = source;
    thread->suspended_general_regs.gregs = gregs;
}

static inline void x86_reset_suspended_general_regs(struct arch_thread *thread)
{
    thread->general_regs_source = X86_GENERAL_REGS_NONE;
    thread->suspended_general_regs.gregs = NULL;
}

__END_CDECLS
