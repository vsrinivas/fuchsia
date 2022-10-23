// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_CPU_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_CPU_H_

#include <lib/zx/result.h>

#include <kernel/cpu.h>

namespace hypervisor {

using percpu_task_t = zx::result<> (*)(void* context, cpu_num_t cpu_num);

// Executes a task on each online CPU, and returns a CPU mask containing each
// CPU the task was successfully run on.
cpu_mask_t percpu_exec(percpu_task_t task, void* context);

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_CPU_H_
