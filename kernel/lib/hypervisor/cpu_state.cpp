// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/atomic.h>
#include <hypervisor/cpu_state.h>

struct percpu_state {
    fbl::atomic<mp_cpu_mask_t> cpu_mask;
    percpu_task_t task;
    void* context;

    percpu_state(percpu_task_t _task, void* _context)
        : cpu_mask(0), task(_task), context(_context) {}
};

static void percpu_task(void* arg) {
    auto state = static_cast<percpu_state*>(arg);
    uint cpu_num = arch_curr_cpu_num();
    mx_status_t status = state->task(state->context, cpu_num);
    if (status == MX_OK)
        state->cpu_mask.fetch_or(1 << cpu_num);
}

mp_cpu_mask_t percpu_exec(percpu_task_t task, void* context) {
    percpu_state state(task, context);
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, percpu_task, &state);
    return state.cpu_mask.load();
}
