// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "el2_cpu_state_priv.h"

#include <arch/arm64/el2_state.h>
#include <arch/arm64/mmu.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <hypervisor/cpu.h>
#include <kernel/mp.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

static fbl::Mutex guest_mutex;
static size_t num_guests TA_GUARDED(guest_mutex) = 0;
static fbl::unique_ptr<El2CpuState> el2_cpu_state TA_GUARDED(guest_mutex);

zx_status_t El2TranslationTable::Init() {
    zx_status_t status = l0_page_.Alloc(0);
    if (status != ZX_OK) {
        return status;
    }
    status = l1_page_.Alloc(0);
    if (status != ZX_OK) {
        return status;
    }

    // L0: Point to a single L1 translation table.
    pte_t* l0_pte = l0_page_.VirtualAddress<pte_t>();
    *l0_pte = l1_page_.PhysicalAddress() | MMU_PTE_L012_DESCRIPTOR_TABLE;

    // L1: Identity map the first 512GB of physical memory at.
    pte_t* l1_pte = l1_page_.VirtualAddress<pte_t>();
    for (size_t i = 0; i < PAGE_SIZE / sizeof(pte_t); i++) {
        l1_pte[i] = i * (1u << 30) | MMU_PTE_ATTR_AF | MMU_PTE_ATTR_SH_INNER_SHAREABLE |
                    MMU_PTE_ATTR_AP_P_RW_U_RW | MMU_PTE_ATTR_NORMAL_MEMORY |
                    MMU_PTE_L012_DESCRIPTOR_BLOCK;
    }

    DMB;
    return ZX_OK;
}

zx_paddr_t El2TranslationTable::Base() const {
    return l0_page_.PhysicalAddress();
}

zx_status_t El2Stack::Alloc() {
    return page_.Alloc(0);
}

zx_paddr_t El2Stack::Top() const {
    return page_.PhysicalAddress() + PAGE_SIZE;
}

zx_status_t El2CpuState::OnTask(void* context, uint cpu_num) {
    auto cpu_state = static_cast<El2CpuState*>(context);
    El2TranslationTable& table = cpu_state->table_;
    El2Stack& stack = cpu_state->stacks_[cpu_num];

    zx_status_t status = arm64_el2_on(table.Base(), stack.Top());
    if (status != ZX_OK) {
        dprintf(CRITICAL, "Failed to turn EL2 on for CPU %u\n", cpu_num);
        return status;
    }

    return ZX_OK;
}

static void el2_off_task(void* arg) {
    zx_status_t status = arm64_el2_off();
    if (status != ZX_OK)
        dprintf(CRITICAL, "Failed to turn EL2 off for CPU %u\n", arch_curr_cpu_num());
}

// static
zx_status_t El2CpuState::Create(fbl::unique_ptr<El2CpuState>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<El2CpuState> cpu_state(new (&ac) El2CpuState);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    zx_status_t status = cpu_state->Init();
    if (status != ZX_OK)
        return status;

    // Initialise the EL2 translation table.
    status = cpu_state->table_.Init();
    if (status != ZX_OK)
        return status;

    // Allocate EL2 stack for each CPU.
    size_t num_cpus = arch_max_num_cpus();
    El2Stack* stacks = new (&ac) El2Stack[num_cpus];
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    fbl::Array<El2Stack> el2_stacks(stacks, num_cpus);
    for (auto& stack : el2_stacks) {
        zx_status_t status = stack.Alloc();
        if (status != ZX_OK)
            return status;
    }
    cpu_state->stacks_ = fbl::move(el2_stacks);

    // Setup EL2 for all online CPUs.
    cpu_mask_t cpu_mask = percpu_exec(OnTask, cpu_state.get());
    if (cpu_mask != mp_get_online_mask()) {
        mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask, el2_off_task, nullptr);
        return ZX_ERR_NOT_SUPPORTED;
    }

    *out = fbl::move(cpu_state);
    return ZX_OK;
}

El2CpuState::~El2CpuState() {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, el2_off_task, nullptr);
}

zx_status_t alloc_vmid(uint8_t* vmid) {
    fbl::AutoLock lock(&guest_mutex);
    if (num_guests == 0) {
        zx_status_t status = El2CpuState::Create(&el2_cpu_state);
        if (status != ZX_OK)
            return status;
    }
    num_guests++;
    return el2_cpu_state->AllocId(vmid);
}

zx_status_t free_vmid(uint8_t vmid) {
    fbl::AutoLock lock(&guest_mutex);
    zx_status_t status = el2_cpu_state->FreeId(vmid);
    if (status != ZX_OK)
        return status;
    num_guests--;
    if (num_guests == 0)
        el2_cpu_state.reset();
    return ZX_OK;
}
