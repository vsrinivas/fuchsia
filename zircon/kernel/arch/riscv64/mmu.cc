// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/heap.h>
#include <lib/ktrace.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/aspace.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <kernel/mutex.h>
#include <ktl/algorithm.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

typedef uintptr_t riscv_pte_t;

riscv_pte_t kernel_pgtable[512] __ALIGNED(PAGE_SIZE);
paddr_t kernel_pgtable_phys; // filled in by start.S

zx_status_t Riscv64ArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count,
                                               uint mmu_flags, size_t* mapped) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                                     size_t* mapped) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::HarvestAccessed(vaddr_t vaddr, size_t count,
                                                 const HarvestCallback& accessed_callback) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::MarkAccessed(vaddr_t vaddr, size_t count) {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Init() {
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Destroy() {
  return ZX_OK;
}

void Riscv64ArchVmAspace::ContextSwitch(Riscv64ArchVmAspace* old_aspace, Riscv64ArchVmAspace* aspace) {
}

void arch_zero_page(void* _ptr) {
}

Riscv64ArchVmAspace::Riscv64ArchVmAspace(vaddr_t base, size_t size, uint mmu_flags, page_alloc_fn_t paf) {}

Riscv64ArchVmAspace::~Riscv64ArchVmAspace() = default;

vaddr_t Riscv64ArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                                      uint next_region_mmu_flags, vaddr_t align, size_t size,
                                      uint mmu_flags) {
  return PAGE_ALIGN(base);
}
