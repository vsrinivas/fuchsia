// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_EL2_CPU_STATE_PRIV_H_
#define ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_EL2_CPU_STATE_PRIV_H_

#include <lib/arch/arm64/system.h>

#include <arch/aspace.h>
#include <fbl/array.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/page.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <ktl/unique_ptr.h>

class El2TranslationTable {
 public:
  El2TranslationTable() = default;
  ~El2TranslationTable();

  zx::result<> Init();
  zx_paddr_t Base() const;

 private:
  // Reset to initial state, releasing all allocated resources.
  void Reset();

  ktl::optional<ArchVmAspace> el2_aspace_;
};

// Represents a stack for use with EL2/
class El2Stack {
 public:
  zx::result<> Alloc();
  zx_paddr_t Top() const;

 private:
  hypervisor::Page page_;
};

// Maintains the EL2 state for each CPU.
class El2CpuState {
 public:
  static zx::result<ktl::unique_ptr<El2CpuState>> Create();
  ~El2CpuState();

  // Allocate/free a VMID.
  zx::result<uint16_t> AllocVmid();
  zx::result<> FreeVmid(uint16_t id);

 private:
  El2TranslationTable table_;
  fbl::Array<El2Stack> stacks_;
  arch::ArmTcrEl2 tcr_;
  arch::ArmVtcrEl2 vtcr_;

  cpu_mask_t cpu_mask_ = 0;
  hypervisor::IdAllocator<uint16_t, UINT16_MAX> vmid_allocator_;

  El2CpuState() = default;

  static zx::result<> OnTask(void* context, cpu_num_t cpu_num);
};

// Allocate and free virtual machine IDs.
zx::result<uint16_t> alloc_vmid();
zx::result<> free_vmid(uint16_t id);

#endif  // ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_EL2_CPU_STATE_PRIV_H_
