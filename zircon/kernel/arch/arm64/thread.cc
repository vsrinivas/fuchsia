// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

#include <arch/arm64.h>
#include <arch/arm64/mp.h>
#include <kernel/thread.h>

#define LOCAL_TRACE 0

// assert that the context switch frame is a multiple of 16 to maintain
// stack alignment requirements per ABI
static_assert(sizeof(arm64_context_switch_frame) % 16 == 0, "");

void arch_thread_initialize(Thread* t, vaddr_t entry_point) {
  // zero out the entire arch state
  t->arch_ = {};

  // create a default stack frame on the stack
  vaddr_t stack_top = t->stack_.top();

  // make sure the top of the stack is 16 byte aligned for EABI compliance
  DEBUG_ASSERT(IS_ALIGNED(stack_top, 16));

  struct arm64_context_switch_frame* frame = (struct arm64_context_switch_frame*)(stack_top);
  frame--;

  // fill in the entry point
  frame->lr = entry_point;

  // This is really a global (boot-time) constant value.
  // But it's stored in each thread struct to satisfy the
  // compiler ABI (TPIDR_EL1 + ZX_TLS_STACK_GUARD_OFFSET).
  t->arch_.stack_guard = Thread::Current::Get()->arch_.stack_guard;

  // set the stack pointer
  t->arch_.sp = (vaddr_t)frame;
#if __has_feature(safe_stack)
  DEBUG_ASSERT(IS_ALIGNED(t->stack_.unsafe_top(), 16));
  t->arch_.unsafe_sp = t->stack_.unsafe_top();
#endif
#if __has_feature(shadow_call_stack)
  // The shadow call stack grows up.
  t->arch_.shadow_call_sp = reinterpret_cast<uintptr_t*>(t->stack_.shadow_call_base());
#endif
}

__NO_SAFESTACK void arch_thread_construct_first(Thread* t) {
  // Propagate the values from the fake arch_thread that the thread
  // pointer points to now (set up in start.S) into the real thread
  // structure being set up now.
  Thread* fake = Thread::Current::Get();
  t->arch_.stack_guard = fake->arch_.stack_guard;
  t->arch_.unsafe_sp = fake->arch_.unsafe_sp;

  // make sure the thread saves a copy of the current cpu pointer
  t->arch_.current_percpu_ptr = arm64_read_percpu_ptr();

  // Force the thread pointer immediately to the real struct.  This way
  // our callers don't have to avoid safe-stack code or risk losing track
  // of the unsafe_sp value.  The caller's unsafe_sp value is visible at
  // TPIDR_EL1 + ZX_TLS_UNSAFE_SP_OFFSET as expected, though TPIDR_EL1
  // happens to have changed.  (We're assuming that the compiler doesn't
  // decide to cache the TPIDR_EL1 value across this function call, which
  // would be pointless since it's just one instruction to fetch it afresh.)
  arch_set_current_thread(t);
}

static void arm64_tpidr_save_state(Thread* thread) {
  thread->arch_.tpidr_el0 = __arm_rsr64("tpidr_el0");
  thread->arch_.tpidrro_el0 = __arm_rsr64("tpidrro_el0");
}

static void arm64_tpidr_restore_state(Thread* thread) {
  __arm_wsr64("tpidr_el0", thread->arch_.tpidr_el0);
  __arm_wsr64("tpidrro_el0", thread->arch_.tpidrro_el0);
}

static void arm64_debug_restore_state(Thread* thread) {
  // If the thread has debug state, then install it, replacing the current contents.
  if (unlikely(thread->arch_.track_debug_state)) {
    arm64_write_hw_debug_regs(&thread->arch_.debug_state);
  }
}

__NO_SAFESTACK void arch_context_switch(Thread* oldthread, Thread* newthread) {
  LTRACEF("old %p (%s), new %p (%s)\n", oldthread, oldthread->name_, newthread, newthread->name_);
  __dsb(ARM_MB_SY); /* broadcast tlb operations in case the thread moves to another cpu */

  /* set the current cpu pointer in the new thread's structure so it can be
   * restored on exception entry.
   */
  newthread->arch_.current_percpu_ptr = arm64_read_percpu_ptr();

  if (likely(!oldthread->user_state_saved_)) {
    arm64_fpu_context_switch(oldthread, newthread);
    arm64_tpidr_save_state(oldthread);
    arm64_tpidr_restore_state(newthread);
    // Not saving debug state because the arch_thread_t's debug state is authoritative.
    arm64_debug_restore_state(newthread);
  } else {
    // Nothing left to save for |oldthread|, so just restore |newthread|.  Technically, we could
    // skip restoring here since we know a higher layer will restore before leaving the kernel.  We
    // restore anyway so we don't leave |oldthread|'s state lingering in the hardware registers.
    // The thinking is that:
    //
    // 1. The performance cost is tolerable - This code path is only executed by threads that have
    // taken a (zircon) exception or are being debugged so there should be no performance impact to
    // "normal" threads.
    //
    // 2. We want to avoid confusion - When, for example, the kernel panics and prints user register
    // state to a log, a future maintainer might be confused to find that some other thread's user
    // register state is present on a CPU that was executing an unrelated thread.
    //
    // 3. We want an extra layer of security - If we make a mistake and don't properly restore the
    // state before returning we might expose one thread's register state to another thread.  By
    // restoring early, that's less likely to happen (think belt and suspenders).
    arm64_fpu_restore_state(newthread);
    arm64_tpidr_restore_state(newthread);
    arm64_debug_restore_state(newthread);
  }

#if __has_feature(shadow_call_stack)
  arm64_context_switch(&oldthread->arch_.sp, newthread->arch_.sp, &oldthread->arch_.shadow_call_sp,
                       newthread->arch_.shadow_call_sp);
#else
  arm64_context_switch(&oldthread->arch_.sp, newthread->arch_.sp);
#endif
}

void arch_dump_thread(Thread* t) {
  if (t->state_ != THREAD_RUNNING) {
    dprintf(INFO, "\tarch: ");
    dprintf(INFO, "sp 0x%lx\n", t->arch_.sp);
  }
}

void* arch_thread_get_blocked_fp(Thread* t) {
  if (!WITH_FRAME_POINTERS) {
    return nullptr;
  }

  struct arm64_context_switch_frame* frame = arm64_get_context_switch_frame(t);
  return (void*)frame->r29;
}

arm64_context_switch_frame* arm64_get_context_switch_frame(Thread* thread) {
  return reinterpret_cast<struct arm64_context_switch_frame*>(thread->arch_.sp);
}

__NO_SAFESTACK void arch_save_user_state(Thread* thread) {
  arm64_fpu_save_state(thread);
  arm64_tpidr_save_state(thread);
  // Not saving debug state because the arch_thread_t's debug state is authoritative.
}

__NO_SAFESTACK void arch_restore_user_state(Thread* thread) {
  arm64_debug_restore_state(thread);
  arm64_fpu_restore_state(thread);
  arm64_tpidr_restore_state(thread);
}

void arch_set_suspended_general_regs(struct Thread* thread, GeneralRegsSource source,
                                     void* iframe) {
  DEBUG_ASSERT(thread->arch_.suspended_general_regs == nullptr);
  DEBUG_ASSERT(iframe != nullptr);
  DEBUG_ASSERT_MSG(source == GeneralRegsSource::Iframe, "invalid source %u\n",
                   static_cast<uint32_t>(source));
  thread->arch_.suspended_general_regs = static_cast<iframe_t*>(iframe);
}

void arch_reset_suspended_general_regs(struct Thread* thread) {
  thread->arch_.suspended_general_regs = nullptr;
}
