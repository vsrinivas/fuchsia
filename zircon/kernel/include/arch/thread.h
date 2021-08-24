// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_THREAD_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_THREAD_H_

#include <arch.h>
#include <zircon/compiler.h>

#include <arch/arch_thread.h>
#include <kernel/thread_lock.h>

struct Thread;

void arch_thread_initialize(Thread*, vaddr_t entry_point);
void arch_context_switch(Thread* oldthread, Thread* newthread) TA_REQ(thread_lock);
void arch_save_user_state(Thread* thread);
void arch_restore_user_state(Thread* thread);
void arch_thread_construct_first(Thread*);
vaddr_t arch_thread_get_blocked_fp(Thread*);

void arch_set_suspended_general_regs(Thread* thread, GeneralRegsSource source, void* gregs);
void arch_reset_suspended_general_regs(Thread* thread);

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_THREAD_H_
