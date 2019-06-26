// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_EL2_CPU_STATE_PRIV_H_
#define ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_EL2_CPU_STATE_PRIV_H_

#include <fbl/array.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/page.h>
#include <kernel/mp.h>
#include <ktl/unique_ptr.h>

class El2TranslationTable {
 public:
  zx_status_t Init();
  zx_paddr_t Base() const;

 private:
  hypervisor::Page l0_page_;
  hypervisor::Page l1_page_;
};

// Represents a stack for use with EL2/
class El2Stack {
 public:
  zx_status_t Alloc();
  zx_paddr_t Top() const;

 private:
  hypervisor::Page page_;
};

// Maintains the EL2 state for each CPU.
class El2CpuState : public hypervisor::IdAllocator<uint8_t, 64> {
 public:
  static zx_status_t Create(ktl::unique_ptr<El2CpuState>* out);
  ~El2CpuState();

 private:
  cpu_mask_t cpu_mask_ = 0;
  El2TranslationTable table_;
  fbl::Array<El2Stack> stacks_;

  El2CpuState() = default;

  static zx_status_t OnTask(void* context, uint cpu_num);
};

// Allocate and free virtual machine IDs.
zx_status_t alloc_vmid(uint8_t* vmid);
zx_status_t free_vmid(uint8_t vmid);

// Allocate and free virtual processor IDs.
zx_status_t alloc_vpid(uint8_t* vpid);
zx_status_t free_vpid(uint8_t vpid);

#endif  // ZIRCON_KERNEL_ARCH_ARM64_HYPERVISOR_EL2_CPU_STATE_PRIV_H_
