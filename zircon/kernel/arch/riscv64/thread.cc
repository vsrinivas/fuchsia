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

void arch_thread_initialize(Thread* t, vaddr_t entry_point) {
}

__NO_SAFESTACK void arch_thread_construct_first(Thread* t) {
}

__NO_SAFESTACK void arch_context_switch(Thread* oldthread, Thread* newthread) {
}

void arch_dump_thread(Thread* t) {
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
