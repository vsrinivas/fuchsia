// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <hypervisor/cpu.h>
#include <kernel/mp.h>
#include <ktl/atomic.h>

#include <ktl/enforce.h>

namespace {

struct percpu_state {
  ktl::atomic<cpu_mask_t> cpu_mask;
  hypervisor::percpu_task_t task;
  void* context;

  percpu_state(hypervisor::percpu_task_t pt, void* cx) : cpu_mask(0), task(pt), context(cx) {}
};

}  // namespace

namespace hypervisor {

static void percpu_task(void* arg) {
  auto state = static_cast<percpu_state*>(arg);
  cpu_num_t cpu_num = arch_curr_cpu_num();
  if (auto result = state->task(state->context, cpu_num); result.is_ok()) {
    state->cpu_mask.fetch_or(cpu_num_to_mask(cpu_num));
  }
}

cpu_mask_t percpu_exec(percpu_task_t task, void* context) {
  percpu_state state(task, context);
  mp_sync_exec(MP_IPI_TARGET_ALL, 0, percpu_task, &state);
  return state.cpu_mask.load();
}

}  // namespace hypervisor
