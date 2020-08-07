// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ASPACE_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ASPACE_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/riscv64/mmu.h>

#include <fbl/canary.h>
#include <kernel/mutex.h>
#include <vm/arch_vm_aspace.h>

class Riscv64ArchVmAspace final : public ArchVmAspaceInterface {
 public:
  Riscv64ArchVmAspace(vaddr_t base, size_t size, uint mmu_flags,
                      page_alloc_fn_t test_paf = nullptr);
  virtual ~Riscv64ArchVmAspace();

  using ArchVmAspaceInterface::page_alloc_fn_t;

  zx_status_t Init() override;

  zx_status_t Destroy() override;

  // main methods
  zx_status_t Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                  size_t* mapped) override;
  zx_status_t MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags,
                            size_t* mapped) override;

  zx_status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) override;

  zx_status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) override;

  zx_status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) override;

  vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                   uint next_region_mmu_flags, vaddr_t align, size_t size, uint mmu_flags) override;

  zx_status_t MarkAccessed(vaddr_t vaddr, size_t count) override;

  zx_status_t HarvestAccessed(vaddr_t vaddr, size_t count,
                              const HarvestCallback& accessed_callback) override;

  zx_status_t FreeUnaccessed(vaddr_t vaddr, size_t count) override { return ZX_ERR_NOT_SUPPORTED; }

  paddr_t arch_table_phys() const override { return 0; }
  uint16_t arch_asid() const { return 0; }
  void arch_set_asid(uint16_t asid) { }

  static void ContextSwitch(Riscv64ArchVmAspace* from, Riscv64ArchVmAspace* to);

  static constexpr bool HasNonTerminalAccessedFlag() { return false; }
};

using ArchVmAspace = Riscv64ArchVmAspace;

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ASPACE_H_
