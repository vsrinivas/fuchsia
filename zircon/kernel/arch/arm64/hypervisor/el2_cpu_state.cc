// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>

#include <arch/arm64/mmu.h>
#include <arch/hypervisor.h>
#include <dev/interrupt.h>
#include <fbl/auto_lock.h>
#include <hypervisor/cpu.h>
#include <kernel/cpu.h>
#include <kernel/mutex.h>
#include <ktl/move.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

#include "el2_cpu_state_priv.h"

namespace {

DECLARE_SINGLETON_MUTEX(GuestMutex);
size_t num_guests TA_GUARDED(GuestMutex::Get()) = 0;
ktl::unique_ptr<El2CpuState> el2_cpu_state TA_GUARDED(GuestMutex::Get());

constexpr size_t kEl2PhysAddressSize = (1ul << MMU_IDENT_SIZE_SHIFT);

// Unmap all mappings everything in the given address space, releasing all resources.
void UnmapAll(ArchVmAspace& aspace) {
  size_t page_count = kEl2PhysAddressSize / PAGE_SIZE;
  zx_status_t result = aspace.Unmap(/*vaddr=*/0, page_count, nullptr);
  DEBUG_ASSERT(result == ZX_OK);
}

// Return true if the given virtual address range is contiguous in physical memory.
bool IsPhysicallyContiguous(vaddr_t base, size_t size) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(base));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(size));

  // Ranges smaller than a page are always physically contiguous.
  if (size <= PAGE_SIZE) {
    return true;
  }

  paddr_t base_addr = vaddr_to_paddr(reinterpret_cast<const void*>(base));
  for (size_t offset = PAGE_SIZE; offset < size; offset += PAGE_SIZE) {
    paddr_t page_addr = vaddr_to_paddr(reinterpret_cast<const void*>(base + offset));
    if (page_addr != base_addr + offset) {
      return false;
    }
  }

  return true;
}

}  // namespace

El2TranslationTable::~El2TranslationTable() { Reset(); }

void El2TranslationTable::Reset() {
  if (el2_aspace_) {
    UnmapAll(*el2_aspace_);
    el2_aspace_->Destroy();
    el2_aspace_.reset();
  }
}

zx_status_t El2TranslationTable::Init() {
  // Create the address space.
  el2_aspace_.emplace(/*base=*/0, /*size=*/kEl2PhysAddressSize, ArmAspaceType::kHypervisor);
  zx_status_t status = el2_aspace_->Init();
  if (status != ZX_OK) {
    el2_aspace_.reset();
    return status;
  }

  // Map in all conventional physical memory read/write.
  size_t num_arenas = pmm_num_arenas();
  for (size_t i = 0; i < num_arenas; i++) {
    pmm_arena_info_t arena;
    pmm_get_arena_info(/*count=*/1, i, &arena, sizeof(arena));
    paddr_t arena_paddr = vaddr_to_paddr(reinterpret_cast<void*>(arena.base));
    size_t page_count = arena.size / PAGE_SIZE;
    status = el2_aspace_->MapContiguous(
        /*vaddr=*/arena_paddr, /*paddr=*/arena_paddr, page_count,
        ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_READ, nullptr);
    if (status != ZX_OK) {
      Reset();
      return status;
    }
  }

  // Map the kernel's code in read/execute.
  vaddr_t code_start = ROUNDDOWN(reinterpret_cast<vaddr_t>(__code_start), PAGE_SIZE);
  vaddr_t code_end = ROUNDUP(reinterpret_cast<vaddr_t>(__code_end), PAGE_SIZE);
  size_t code_size = code_end - code_start;
  DEBUG_ASSERT(IsPhysicallyContiguous(code_start, code_size));
  paddr_t code_start_paddr = vaddr_to_paddr(reinterpret_cast<const void*>(code_start));
  status = el2_aspace_->Protect(code_start_paddr, code_size / PAGE_SIZE,
                                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_EXECUTE);
  if (status != ZX_OK) {
    Reset();
    return status;
  }

  return ZX_OK;
}

zx_paddr_t El2TranslationTable::Base() const { return el2_aspace_->arch_table_phys(); }

zx_status_t El2Stack::Alloc() { return page_.Alloc(0); }

zx_paddr_t El2Stack::Top() const { return page_.PhysicalAddress() + PAGE_SIZE; }

zx_status_t El2CpuState::OnTask(void* context, cpu_num_t cpu_num) {
  auto cpu_state = static_cast<El2CpuState*>(context);
  El2TranslationTable& table = cpu_state->table_;
  El2Stack& stack = cpu_state->stacks_[cpu_num];
  zx_status_t status = arm64_el2_on(table.Base(), stack.Top());
  if (status != ZX_OK) {
    dprintf(CRITICAL, "Failed to turn EL2 on for CPU %u\n", cpu_num);
    return status;
  }
  unmask_interrupt(kMaintenanceVector);
  unmask_interrupt(kTimerVector);
  return ZX_OK;
}

static void el2_off_task(void* arg) {
  mask_interrupt(kTimerVector);
  mask_interrupt(kMaintenanceVector);
  zx_status_t status = arm64_el2_off();
  if (status != ZX_OK) {
    dprintf(CRITICAL, "Failed to turn EL2 off for CPU %u\n", arch_curr_cpu_num());
  }
}

// static
zx_status_t El2CpuState::Create(ktl::unique_ptr<El2CpuState>* out) {
  fbl::AllocChecker ac;
  ktl::unique_ptr<El2CpuState> cpu_state(new (&ac) El2CpuState);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Initialise the EL2 translation table.
  zx_status_t status = cpu_state->table_.Init();
  if (status != ZX_OK) {
    return status;
  }

  // Allocate EL2 stack for each CPU.
  size_t num_cpus = arch_max_num_cpus();
  El2Stack* stacks = new (&ac) El2Stack[num_cpus];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  fbl::Array<El2Stack> el2_stacks(stacks, num_cpus);
  for (auto& stack : el2_stacks) {
    status = stack.Alloc();
    if (status != ZX_OK) {
      return status;
    }
  }
  cpu_state->stacks_ = ktl::move(el2_stacks);

  // Setup EL2 for all online CPUs.
  cpu_state->cpu_mask_ = hypervisor::percpu_exec(OnTask, cpu_state.get());
  if (cpu_state->cpu_mask_ != mp_get_online_mask()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  *out = ktl::move(cpu_state);
  return ZX_OK;
}

El2CpuState::~El2CpuState() { mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask_, el2_off_task, nullptr); }

zx_status_t El2CpuState::AllocVmid(uint8_t* vmid) { return id_allocator_.AllocId(vmid); }

zx_status_t El2CpuState::FreeVmid(uint8_t vmid) { return id_allocator_.FreeId(vmid); }

zx_status_t alloc_vmid(uint8_t* vmid) {
  Guard<Mutex> guard(GuestMutex::Get());
  if (num_guests == 0) {
    zx_status_t status = El2CpuState::Create(&el2_cpu_state);
    if (status != ZX_OK) {
      return status;
    }
  }
  num_guests++;
  return el2_cpu_state->AllocVmid(vmid);
}

zx_status_t free_vmid(uint8_t vmid) {
  Guard<Mutex> guard(GuestMutex::Get());
  zx_status_t status = el2_cpu_state->FreeVmid(vmid);
  if (status != ZX_OK) {
    return status;
  }
  num_guests--;
  if (num_guests == 0) {
    el2_cpu_state.reset();
  }
  return ZX_OK;
}
