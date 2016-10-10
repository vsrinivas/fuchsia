// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <debug.h>
#include <trace.h>
#include <kernel/thread.h>
#include <arch/arm.h>

struct context_switch_frame {
    vaddr_t cp15_rw;
    vaddr_t cp15_ro;
    vaddr_t r4;
    vaddr_t r5;
    vaddr_t r6;
    vaddr_t r7;
    vaddr_t r8;
    vaddr_t r9;
    vaddr_t r10;
    vaddr_t r11;
    vaddr_t lr;
};

extern void arm_context_switch(addr_t *old_sp, addr_t new_sp);

void arch_thread_initialize(thread_t *t, vaddr_t entry_point)
{
    // create a default stack frame on the stack
    vaddr_t stack_top = (vaddr_t)t->stack + t->stack_size;

    // make sure the top of the stack is 8 byte aligned for EABI compliance
    stack_top = ROUNDDOWN(stack_top, 8);
    t->stack_top = stack_top;

    struct context_switch_frame *frame = (struct context_switch_frame *)(stack_top);
    frame--;

    // fill it in
    memset(frame, 0, sizeof(*frame));
    frame->lr = (vaddr_t)entry_point;

    // set the stack pointer
    t->arch.sp = (vaddr_t)frame;

#if ARM_WITH_VFP
    arm_fpu_thread_initialize(t);
#endif
}

void arch_context_switch(thread_t *oldthread, thread_t *newthread)
{
//  TRACEF("arch_context_switch: cpu %u old %p (%s), new %p (%s)\n", arch_curr_cpu_num(), oldthread, oldthread->name, newthread, newthread->name);
#if ARM_WITH_VFP
    arm_fpu_thread_swap(oldthread, newthread);
#endif

    arm_context_switch(&oldthread->arch.sp, newthread->arch.sp);
}

void arch_dump_thread(thread_t *t)
{
    if (t->state != THREAD_RUNNING) {
        dprintf(INFO, "\tarch: ");
        dprintf(INFO, "sp %#" PRIxPTR "\n", t->arch.sp);
    }
}
