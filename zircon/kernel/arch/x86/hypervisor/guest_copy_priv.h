// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_GUEST_COPY_PRIV_H_
#define ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_GUEST_COPY_PRIV_H_

#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/tlb.h>

struct GuestPageTable {
  hypervisor::DefaultTlb& tlb;
  hypervisor::GuestPhysicalAddressSpace& gpas;
  const zx_gpaddr_t cr3;
};

#endif  // ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_GUEST_COPY_PRIV_H_
