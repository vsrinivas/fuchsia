// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <debug.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>

#include <arch/riscv64.h>
#include <arch/riscv64/mp.h>
#include <kernel/thread.h>

#define LOCAL_TRACE 0

// assert that the context switch frame is a multiple of 16 to maintain
// stack alignment requirements per ABI
static_assert(sizeof(riscv64_context_switch_frame) % 16 == 0, "");

void arch_thread_initialize(Thread* t, vaddr_t entry_point) {
  // zero out the entire arch state
  t->arch() = {};

  // create a default stack frame on the stack
  vaddr_t stack_top = t->stack().top();

  // make sure the top of the stack is 16 byte aligned for EABI compliance
  DEBUG_ASSERT(IS_ALIGNED(stack_top, 16));

  struct riscv64_context_switch_frame* frame = (struct riscv64_context_switch_frame*)(stack_top);
  frame--;

  // fill in the entry point
  frame->ra = entry_point;

  // set the stack pointer
  t->arch().sp = (vaddr_t)frame;
}

__NO_SAFESTACK void arch_thread_construct_first(Thread* t) {
  // make sure the thread saves a copy of the current cpu pointer
  t->arch().current_percpu_ptr = riscv64_get_percpu();
  __asm__ volatile(
      "mv   %0, sp"
      : "=r" (t->arch().sp)
      :: "memory");
}

__NO_SAFESTACK void arch_context_switch(Thread* oldthread, Thread* newthread) {
  DEBUG_ASSERT(arch_ints_disabled());

  LTRACEF("old %p (%s), new %p (%s)\n", oldthread, oldthread->name(), newthread, newthread->name());

  /* set the current cpu pointer in the new thread's structure so it can be
   * restored on exception entry.
   */
  newthread->arch().current_percpu_ptr = riscv64_get_percpu();

  riscv64_context_switch(&oldthread->arch().sp, newthread->arch().sp);
}

void arch_dump_thread(Thread* t) {
  if (t->state() != THREAD_RUNNING) {
    dprintf(INFO, "\tarch: ");
    dprintf(INFO, "sp 0x%lx\n", t->arch().sp);
  }
}

void* arch_thread_get_blocked_fp(Thread* t) {
  return 0;
}

__NO_SAFESTACK void arch_save_user_state(Thread* thread) {
}

__NO_SAFESTACK void arch_restore_user_state(Thread* thread) {
}

void arch_set_suspended_general_regs(struct Thread* thread, GeneralRegsSource source,
                                     void* iframe) {
}

void arch_reset_suspended_general_regs(struct Thread* thread) {
}
