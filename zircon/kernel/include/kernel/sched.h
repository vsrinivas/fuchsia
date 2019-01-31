// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <kernel/thread.h>
#include <list.h>
#include <stdbool.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// scheduler interface, used internally by thread.c
// not intended to be used by regular kernel code
void sched_init_early(void);

void sched_init_thread(thread_t* t, int priority);
void sched_block(void) TA_REQ(thread_lock);
void sched_yield(void) TA_REQ(thread_lock);
void sched_preempt(void) TA_REQ(thread_lock);
void sched_reschedule(void) TA_REQ(thread_lock);
void sched_resched_internal(void) TA_REQ(thread_lock);
void sched_unblock_idle(thread_t* t) TA_REQ(thread_lock);
void sched_migrate(thread_t* t) TA_REQ(thread_lock);

// set the inherited priority of a thread and return if the caller should locally reschedule.
// pri should be <= MAX_PRIORITY, negative values disable priority inheritance.
void sched_inherit_priority(thread_t* t, int pri, bool* local_resched) TA_REQ(thread_lock);

// set the priority of a thread and reset the boost value. This function might reschedule.
// pri should be 0 <= to <= MAX_PRIORITY.
void sched_change_priority(thread_t* t, int pri) TA_REQ(thread_lock);

// return true if the thread was placed on the current cpu's run queue
// this usually means the caller should locally reschedule soon
bool sched_unblock(thread_t* t) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
bool sched_unblock_list(struct list_node* list) __WARN_UNUSED_RESULT TA_REQ(thread_lock);

void sched_transition_off_cpu(cpu_num_t old_cpu) TA_REQ(thread_lock);

// sched_preempt_timer_tick is called when the preemption timer for a CPU has fired.
//
// This function is logically private and should only be called by timer.cpp.
void sched_preempt_timer_tick(zx_time_t now);

__END_CDECLS
