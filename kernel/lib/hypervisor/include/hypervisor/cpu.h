// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/cpu.h>
#include <kernel/thread.h>

typedef zx_status_t (* percpu_task_t)(void* context, cpu_num_t cpu_num);

/* Executes a task on each online CPU, and returns a CPU mask containing each
 * CPU the task was successfully run on. */
cpu_mask_t percpu_exec(percpu_task_t task, void* context);

/* Pin the current thread to a CPU, and reschedule it to execute on that CPU. */
thread_t* pin_thread(cpu_num_t cpu);
