// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64.h>
#include <arch/arm64/mp.h>
#include <debug.h>
#include <kernel/thread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

#define LOCAL_TRACE 0

// assert that the context switch frame is a multiple of 16 to maintain
// stack alignment requirements per ABI
static_assert(sizeof(arm64_context_switch_frame) % 16 == 0, "");

extern void arm64_context_switch(addr_t* old_sp, addr_t new_sp);

void arch_thread_initialize(thread_t* t, vaddr_t entry_point) {
    // zero out the entire arch state
    t->arch = {};

    // create a default stack frame on the stack
    vaddr_t stack_top = t->stack.top;

    // make sure the top of the stack is 16 byte aligned for EABI compliance
    stack_top = ROUNDDOWN(stack_top, 16);
    t->stack.top = stack_top;

    struct arm64_context_switch_frame* frame = (struct arm64_context_switch_frame*)(stack_top);
    frame--;

    // fill in the entry point
    frame->lr = entry_point;

    // This is really a global (boot-time) constant value.
    // But it's stored in each thread struct to satisfy the
    // compiler ABI (TPIDR_EL1 + ZX_TLS_STACK_GUARD_OFFSET).
    t->arch.stack_guard = get_current_thread()->arch.stack_guard;

    // set the stack pointer
    t->arch.sp = (vaddr_t)frame;
#if __has_feature(safe_stack)
    t->arch.unsafe_sp =
        ROUNDDOWN(t->stack.unsafe_base + t->stack.size, 16);
#endif

    // Initialize the debug state to a valid initial state.
    for (size_t i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
        t->arch.debug_state.hw_bps[i].dbgbcr = 0;
        t->arch.debug_state.hw_bps[i].dbgbvr = 0;
    }
}

__NO_SAFESTACK void arch_thread_construct_first(thread_t* t) {
    // Propagate the values from the fake arch_thread that the thread
    // pointer points to now (set up in start.S) into the real thread
    // structure being set up now.
    thread_t* fake = get_current_thread();
    t->arch.stack_guard = fake->arch.stack_guard;
    t->arch.unsafe_sp = fake->arch.unsafe_sp;

    // make sure the thread saves a copy of the current cpu pointer
    t->arch.current_percpu_ptr = arm64_read_percpu_ptr();

    // Force the thread pointer immediately to the real struct.  This way
    // our callers don't have to avoid safe-stack code or risk losing track
    // of the unsafe_sp value.  The caller's unsafe_sp value is visible at
    // TPIDR_EL1 + ZX_TLS_UNSAFE_SP_OFFSET as expected, though TPIDR_EL1
    // happens to have changed.  (We're assuming that the compiler doesn't
    // decide to cache the TPIDR_EL1 value across this function call, which
    // would be pointless since it's just one instruction to fetch it afresh.)
    set_current_thread(t);
}

__NO_SAFESTACK void arch_context_switch(thread_t* oldthread,
                                        thread_t* newthread) {
    LTRACEF("old %p (%s), new %p (%s)\n", oldthread, oldthread->name, newthread, newthread->name);
    __dsb(ARM_MB_SY); /* broadcast tlb operations in case the thread moves to another cpu */

    /* set the current cpu pointer in the new thread's structure so it can be
     * restored on exception entry.
     */
    newthread->arch.current_percpu_ptr = arm64_read_percpu_ptr();

    arm64_fpu_context_switch(oldthread, newthread);
    arm64_debug_state_context_switch(oldthread, newthread);
    arm64_context_switch(&oldthread->arch.sp, newthread->arch.sp);
}

void arch_dump_thread(thread_t* t) {
    if (t->state != THREAD_RUNNING) {
        dprintf(INFO, "\tarch: ");
        dprintf(INFO, "sp 0x%lx\n", t->arch.sp);
    }
}

void* arch_thread_get_blocked_fp(struct thread* t) {
    if (!WITH_FRAME_POINTERS)
        return nullptr;

    struct arm64_context_switch_frame* frame = arm64_get_context_switch_frame(t);
    return (void*)frame->r29;
}

void arm64_debug_state_context_switch(thread *old_thread, thread *new_thread) {
    // If the new thread has debug state, then install it, replacing the current contents.
    if (unlikely(new_thread->arch.track_debug_state)) {
        arm64_write_hw_debug_regs(&new_thread->arch.debug_state);
        arm64_set_debug_state_for_cpu(true);
        return;
    }

    // If the old thread had debug state running and the new one doesn't use it, disable the
    // debug capabilities. We don't need to clear the state because if a new thread being
    // scheduled needs them, then it will overwrite the state.
    if (unlikely(old_thread->arch.track_debug_state)) {
        arm64_set_debug_state_for_cpu(false);
    }
}

arm64_context_switch_frame* arm64_get_context_switch_frame(struct thread* thread) {
    return reinterpret_cast<struct arm64_context_switch_frame*>(thread->arch.sp);
}
