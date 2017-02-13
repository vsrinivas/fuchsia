// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <stdbool.h>
#include <list.h>
#include <kernel/thread.h>

/* scheduler interface, used internally by thread.c */
/* not intended to be used by regular kernel code */
void sched_init_early(void);

void sched_block(void);
void sched_unblock(thread_t *t);
void sched_unblock_list(struct list_node *list);
void sched_yield(void);
void sched_preempt(void);
void sched_reschedule(void);

/* the low level reschedule routine, called from the scheduler */
void _thread_resched_internal(void);

thread_t *sched_get_top_thread(uint cpu);
