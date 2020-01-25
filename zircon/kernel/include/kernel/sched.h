// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SCHED_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SCHED_H_

#include <list.h>
#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/scheduler.h>

#include <kernel/thread.h>

__BEGIN_CDECLS

void sched_init_thread(thread_t* t, int priority);
void sched_block(void) TA_REQ(thread_lock);
void sched_yield(void) TA_REQ(thread_lock);
void sched_preempt(void) TA_REQ(thread_lock);
void sched_reschedule(void) TA_REQ(thread_lock);
void sched_resched_internal(void) TA_REQ(thread_lock);
void sched_unblock_idle(thread_t* t) TA_REQ(thread_lock);
void sched_migrate(thread_t* t) TA_REQ(thread_lock);

// Set the inherited priority of a thread.
//
// Update a mask of affected CPUs along with a flag indicating whether or not a
// local reschedule is needed.  After the caller has finished any batch update
// operations, it is their responsibility to trigger reschedule operations on
// the local CPU (if needed) as well as any other CPUs.  This allows callers to
// bacth update the state of several threads in a priority inheritance chain
// before finally rescheduling.
void sched_inherit_priority(thread_t* t, int pri, bool* local_resched, cpu_mask_t* accum_cpu_mask)
    TA_REQ(thread_lock);

// set the priority of a thread and reset the boost value. This function might reschedule.
// pri should be 0 <= to <= MAX_PRIORITY.
void sched_change_priority(thread_t* t, int pri) TA_REQ(thread_lock);

// set the deadline of a thread. This function might reschedule.
// requires 0 < capacity <= relative_deadline <= period.
void sched_change_deadline(thread_t* t, const zx_sched_deadline_params_t& params)
    TA_REQ(thread_lock);

// return true if the thread was placed on the current cpu's run queue
// this usually means the caller should locally reschedule soon
bool sched_unblock(thread_t* t) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
bool sched_unblock_list(struct list_node* list) __WARN_UNUSED_RESULT TA_REQ(thread_lock);

void sched_transition_off_cpu(cpu_num_t old_cpu) TA_REQ(thread_lock);

// sched_preempt_timer_tick is called when the preemption timer for a CPU has fired.
//
// This function is logically private and should only be called by timer.cpp.
void sched_preempt_timer_tick(zx_time_t now);

// Ensure this define has a value when not defined globally by the build system.
#ifndef SCHEDULER_TRACING_LEVEL
#define SCHEDULER_TRACING_LEVEL 0
#endif

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SCHED_H_
