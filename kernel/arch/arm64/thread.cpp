// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <debug.h>
#include <trace.h>
#include <kernel/thread.h>
#include <arch/arm64.h>
#include <arch/arm64/mp.h>

#define LOCAL_TRACE 0

// Register state layout used by arm64_context_switch().
struct context_switch_frame {
    uint64_t tpidr_el0;
    uint64_t tpidrro_el0;
    uint64_t r19;
    uint64_t r20;
    uint64_t r21;
    uint64_t r22;
    uint64_t r23;
    uint64_t r24;
    uint64_t r25;
    uint64_t r26;
    uint64_t r27;
    uint64_t r28;
    uint64_t r29;
    uint64_t lr;
};

// assert that the context switch frame is a multiple of 16 to maintain
// stack alignment requirements per ABI
static_assert(sizeof(context_switch_frame) % 16 == 0, "");

extern void arm64_context_switch(addr_t *old_sp, addr_t new_sp);

void arch_thread_initialize(thread_t *t, vaddr_t entry_point)
{
    // zero out the entire arch state
    t->arch = {};

    // create a default stack frame on the stack
    vaddr_t stack_top = (vaddr_t)t->stack + t->stack_size;

    // make sure the top of the stack is 16 byte aligned for EABI compliance
    stack_top = ROUNDDOWN(stack_top, 16);
    t->stack_top = stack_top;

    struct context_switch_frame *frame = (struct context_switch_frame *)(stack_top);
    frame--;

    // fill in the entry point
    frame->lr = entry_point;

    // This is really a global (boot-time) constant value.
    // But it's stored in each thread struct to satisfy the
    // compiler ABI (TPIDR_EL1 + MX_TLS_STACK_GUARD_OFFSET).
    t->arch.stack_guard = get_current_thread()->arch.stack_guard;

    // set the stack pointer
    t->arch.sp = (vaddr_t)frame;
#if __has_feature(safe_stack)
    t->arch.unsafe_sp =
        ROUNDDOWN((vaddr_t)t->unsafe_stack + t->stack_size, 16);
#endif
}

__NO_SAFESTACK void arch_thread_construct_first(thread_t *t)
{
    // Propagate the values from the fake arch_thread that the thread
    // pointer points to now (set up in start.S) into the real thread
    // structure being set up now.
    thread_t *fake = get_current_thread();
    t->arch.stack_guard = fake->arch.stack_guard;
    t->arch.unsafe_sp = fake->arch.unsafe_sp;

    // make sure the thread saves a copy of the current cpu pointer
    t->arch.current_percpu_ptr = arm64_read_percpu_ptr();

    // Force the thread pointer immediately to the real struct.  This way
    // our callers don't have to avoid safe-stack code or risk losing track
    // of the unsafe_sp value.  The caller's unsafe_sp value is visible at
    // TPIDR_EL1 + MX_TLS_UNSAFE_SP_OFFSET as expected, though TPIDR_EL1
    // happens to have changed.  (We're assuming that the compiler doesn't
    // decide to cache the TPIDR_EL1 value across this function call, which
    // would be pointless since it's just one instruction to fetch it afresh.)
    set_current_thread(t);
}

__NO_SAFESTACK void arch_context_switch(thread_t *oldthread,
                                        thread_t *newthread)
{
    LTRACEF("old %p (%s), new %p (%s)\n", oldthread, oldthread->name, newthread, newthread->name);
    DSB; /* broadcast tlb operations in case the thread moves to another cpu */

    /* set the current cpu pointer in the new thread's structure so it can be
     * restored on exception entry.
     */
    newthread->arch.current_percpu_ptr = arm64_read_percpu_ptr();

    arm64_fpu_context_switch(oldthread, newthread);
    arm64_context_switch(&oldthread->arch.sp, newthread->arch.sp);
}

void arch_dump_thread(thread_t *t)
{
    if (t->state != THREAD_RUNNING) {
        dprintf(INFO, "\tarch: ");
        dprintf(INFO, "sp 0x%lx\n", t->arch.sp);
    }
}
