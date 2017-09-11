// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "el2_cpu_state_priv.h"

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <vm/pmm.h>

static fbl::Mutex el2_mutex;
static size_t num_guests TA_GUARDED(el2_mutex) = 0;
static fbl::unique_ptr<El2CpuState> el2_cpu_state TA_GUARDED(el2_mutex);

static mx_status_t el2_set_stack(mx_paddr_t stack_top) {
    register mx_status_t status asm("x0") = MX_OK;
    __asm__ volatile("hvc #0" ::: "x0");
    return status;
}

El2Stack::~El2Stack() {
    if (stack_paddr_ != 0)
        pmm_free_kpages(paddr_to_kvaddr(stack_paddr_), ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE);
}

mx_status_t El2Stack::Alloc() {
    pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE, nullptr, &stack_paddr_);
    return stack_paddr_ != 0 ? MX_OK : MX_ERR_NO_MEMORY;
}

mx_paddr_t El2Stack::Top() const {
    return stack_paddr_ + ARCH_DEFAULT_STACK_SIZE;
}

static mx_status_t el2_on_task(void* context, uint cpu_num) {
    auto stacks = static_cast<fbl::Array<El2Stack>*>(context);
    El2Stack& stack = (*stacks)[cpu_num];

    mx_status_t status = el2_set_stack(stack.Top());
    if (status != MX_OK) {
        dprintf(CRITICAL, "Failed to set EL2 stack for CPU %u\n", cpu_num);
        return status;
    }

    return MX_OK;
}

static void el2_off_task(void* arg) {
    mx_status_t status = el2_set_stack(0);
    if (status != MX_OK)
        dprintf(CRITICAL, "Failed to clear EL2 stack for CPU %u\n", arch_curr_cpu_num());
}

// static
mx_status_t El2CpuState::Create(fbl::unique_ptr<El2CpuState>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<El2CpuState> el2_cpu_state(new (&ac) El2CpuState);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    mx_status_t status = el2_cpu_state->Init();
    if (status != MX_OK)
        return status;

    // Allocate EL2 stack for each CPU.
    size_t num_cpus = arch_max_num_cpus();
    El2Stack* stacks = new (&ac) El2Stack[num_cpus];
    if (!ac.check())
        return MX_ERR_NO_MEMORY;
    fbl::Array<El2Stack> el2_stacks(stacks, num_cpus);
    for (auto& stack : el2_stacks) {
        mx_status_t status = stack.Alloc();
        if (status != MX_OK)
            return status;
    }

    // Setup EL2 for all online CPUs.
    mp_cpu_mask_t cpu_mask = percpu_exec(el2_on_task, &el2_stacks);
    if (cpu_mask != mp_get_online_mask()) {
        mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask, el2_off_task, nullptr);
        return MX_ERR_NOT_SUPPORTED;
    }

    el2_cpu_state->el2_stacks_ = fbl::move(el2_stacks);
    *out = fbl::move(el2_cpu_state);
    return MX_OK;
}

El2CpuState::~El2CpuState() {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, el2_off_task, nullptr);
}

mx_status_t alloc_vmid(uint8_t* vmid) {
    fbl::AutoLock lock(&el2_mutex);
    if (num_guests == 0) {
        mx_status_t status = El2CpuState::Create(&el2_cpu_state);
        if (status != MX_OK)
            return status;
    }
    num_guests++;
    return el2_cpu_state->AllocId(vmid);
}

mx_status_t free_vmid(uint8_t vmid) {
    fbl::AutoLock lock(&el2_mutex);
    mx_status_t status = el2_cpu_state->FreeId(vmid);
    if (status != MX_OK)
        return status;
    num_guests--;
    if (num_guests == 0)
        el2_cpu_state.reset();
    return MX_OK;
}
