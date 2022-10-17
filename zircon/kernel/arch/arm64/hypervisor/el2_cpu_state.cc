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

#include <ktl/enforce.h>

namespace {

DECLARE_SINGLETON_MUTEX(GuestMutex);
size_t num_guests TA_GUARDED(GuestMutex::Get()) = 0;
ktl::unique_ptr<El2CpuState> el2_cpu_state TA_GUARDED(GuestMutex::Get());

constexpr size_t kEl2PhysAddressSize = (1ul << MMU_IDENT_SIZE_SHIFT);

// Unmap all mappings everything in the given address space, releasing all resources.
void UnmapAll(ArchVmAspace& aspace) {
  size_t page_count = kEl2PhysAddressSize / PAGE_SIZE;
  zx_status_t result =
      aspace.Unmap(/*vaddr=*/0, page_count, ArchVmAspace::EnlargeOperation::Yes, nullptr);
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

zx::result<> El2TranslationTable::Init() {
  // Create the address space.
  el2_aspace_.emplace(/*base=*/0, /*size=*/kEl2PhysAddressSize, ArmAspaceType::kHypervisor);
  zx_status_t status = el2_aspace_->Init();
  if (status != ZX_OK) {
    el2_aspace_.reset();
    return zx::error(status);
  }

  // Map in all conventional physical memory read/write.
  size_t num_arenas = pmm_num_arenas();
  for (size_t i = 0; i < num_arenas; i++) {
    pmm_arena_info_t arena;
    pmm_get_arena_info(/*count=*/1, i, &arena, sizeof(arena));
    paddr_t arena_paddr = arena.base;
    size_t page_count = arena.size / PAGE_SIZE;
    status = el2_aspace_->MapContiguous(
        /*vaddr=*/arena_paddr, /*paddr=*/arena_paddr, page_count,
        ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_READ, nullptr);
    if (status != ZX_OK) {
      Reset();
      return zx::error(status);
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
    return zx::error(status);
  }

  return zx::ok();
}

zx_paddr_t El2TranslationTable::Base() const { return el2_aspace_->arch_table_phys(); }

zx::result<> El2Stack::Alloc() { return page_.Alloc(0); }

zx_paddr_t El2Stack::Top() const { return page_.PhysicalAddress() + PAGE_SIZE; }

zx::result<> El2CpuState::OnTask(void* context, cpu_num_t cpu_num) {
  auto cpu_state = static_cast<El2CpuState*>(context);
  El2TranslationTable& table = cpu_state->table_;
  El2Stack& stack = cpu_state->stacks_[cpu_num];
  auto tcr = cpu_state->tcr_.reg_value();
  auto vtcr = cpu_state->vtcr_.reg_value();
  zx_status_t status = arm64_el2_on(table.Base(), stack.Top(), tcr, vtcr);
  if (status != ZX_OK) {
    dprintf(CRITICAL, "Failed to turn EL2 on for CPU %u\n", cpu_num);
    return zx::error(status);
  }
  unmask_interrupt(kMaintenanceVector);
  return zx::ok();
}

static void el2_off_task(void* arg) {
  mask_interrupt(kMaintenanceVector);
  zx_status_t status = arm64_el2_off();
  if (status != ZX_OK) {
    dprintf(CRITICAL, "Failed to turn EL2 off for CPU %u\n", arch_curr_cpu_num());
  }
}

// static
zx::result<ktl::unique_ptr<El2CpuState>> El2CpuState::Create() {
  fbl::AllocChecker ac;
  ktl::unique_ptr<El2CpuState> cpu_state(new (&ac) El2CpuState);
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // Initialise the EL2 translation table.
  if (auto result = cpu_state->table_.Init(); result.is_error()) {
    return result.take_error();
  }

  // Allocate EL2 stack for each CPU.
  size_t num_cpus = arch_max_num_cpus();
  El2Stack* stacks = new (&ac) El2Stack[num_cpus];
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  fbl::Array<El2Stack> el2_stacks(stacks, num_cpus);
  for (auto& stack : el2_stacks) {
    if (auto result = stack.Alloc(); result.is_error()) {
      return result.take_error();
    }
  }
  cpu_state->stacks_ = ktl::move(el2_stacks);

  // Setup TCR_EL2 and VTCR_EL2.
  auto address_size = arch::ArmIdAa64Mmfr0El1::Read().pa_range();
  cpu_state->tcr_.set_reg_value(MMU_TCR_EL2_FLAGS);
  cpu_state->tcr_.set_ps(address_size);
  cpu_state->vtcr_.set_reg_value(MMU_VTCR_EL2_FLAGS);
  cpu_state->vtcr_.set_ps(address_size);
  if (arch::ArmIdAa64Mmfr1El1::Read().vmid_bits() == arch::ArmAsidSize::k16bits) {
    cpu_state->vtcr_.set_vs(true);
  } else if (auto result = cpu_state->vmid_allocator_.Reset(UINT8_MAX); result.is_error()) {
    return result.take_error();
  }

  // Setup EL2 for all online CPUs.
  cpu_state->cpu_mask_ = hypervisor::percpu_exec(OnTask, cpu_state.get());
  if (cpu_state->cpu_mask_ != mp_get_online_mask()) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok(ktl::move(cpu_state));
}

El2CpuState::~El2CpuState() { mp_sync_exec(MP_IPI_TARGET_MASK, cpu_mask_, el2_off_task, nullptr); }

zx::result<uint16_t> El2CpuState::AllocVmid() { return vmid_allocator_.TryAlloc(); }

zx::result<> El2CpuState::FreeVmid(uint16_t id) { return vmid_allocator_.Free(id); }

zx::result<uint16_t> alloc_vmid() {
  Guard<Mutex> guard(GuestMutex::Get());
  if (num_guests == 0) {
    auto cpu_state = El2CpuState::Create();
    if (cpu_state.is_error()) {
      return cpu_state.take_error();
    }
    el2_cpu_state = ktl::move(*cpu_state);
  }
  num_guests++;
  return el2_cpu_state->AllocVmid();
}

zx::result<> free_vmid(uint16_t id) {
  Guard<Mutex> guard(GuestMutex::Get());
  if (auto result = el2_cpu_state->FreeVmid(id); result.is_error()) {
    return result.take_error();
  }
  num_guests--;
  if (num_guests == 0) {
    el2_cpu_state.reset();
  }
  return zx::ok();
}
