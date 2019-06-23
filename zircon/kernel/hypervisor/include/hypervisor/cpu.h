// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/cpu.h>
#include <kernel/thread.h>

namespace hypervisor {

typedef zx_status_t (* percpu_task_t)(void* context, cpu_num_t cpu_num);

// Executes a task on each online CPU, and returns a CPU mask containing each
// CPU the task was successfully run on.
cpu_mask_t percpu_exec(percpu_task_t task, void* context);

// CPU corresponding to the given VPID.
cpu_num_t cpu_of(uint16_t vpid);

// Pin the current thread to a CPU, based on the given VPID, and reschedule it
// to execute on that CPU.
thread_t* pin_thread(uint16_t vpid);

// Check that the current thread is correctly pinned, based on the given VPID.
bool check_pinned_cpu_invariant(uint16_t vpid, const thread_t* thread);

} // namespace hypervisor
