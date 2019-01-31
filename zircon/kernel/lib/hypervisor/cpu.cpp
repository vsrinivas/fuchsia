// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <fbl/atomic.h>
#include <hypervisor/cpu.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/thread.h>

namespace {

struct percpu_state {
    fbl::atomic<cpu_mask_t> cpu_mask;
    hypervisor::percpu_task_t task;
    void* context;

    percpu_state(hypervisor::percpu_task_t _task, void* _context)
        : cpu_mask(0), task(_task), context(_context) {}
};

} // namespace

namespace hypervisor {

static void percpu_task(void* arg) {
    auto state = static_cast<percpu_state*>(arg);
    cpu_num_t cpu_num = arch_curr_cpu_num();
    zx_status_t status = state->task(state->context, cpu_num);
    if (status == ZX_OK)
        state->cpu_mask.fetch_or(cpu_num_to_mask(cpu_num));
}

cpu_mask_t percpu_exec(percpu_task_t task, void* context) {
    percpu_state state(task, context);
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, percpu_task, &state);
    return state.cpu_mask.load();
}

cpu_num_t cpu_of(uint16_t vpid) {
    return (vpid - 1) % arch_max_num_cpus();
}

thread_t* pin_thread(uint16_t vpid) {
    thread_t* thread = get_current_thread();
    thread_set_cpu_affinity(thread, cpu_num_to_mask(cpu_of(vpid)));
    return thread;
}

bool check_pinned_cpu_invariant(uint16_t vpid, const thread_t* thread) {
    cpu_num_t cpu = cpu_of(vpid);
    return thread == get_current_thread() &&
           thread->cpu_affinity & cpu_num_to_mask(cpu) &&
           arch_curr_cpu_num() == cpu;
}

} // namespace hypervisor
