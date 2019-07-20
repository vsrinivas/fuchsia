// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_PVCLOCK_PRIV_H_
#define ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_PVCLOCK_PRIV_H_

#include <zircon/types.h>

#include <hypervisor/guest_physical_address_space.h>

struct PvClockState;

// This structure contains mapping between TSC and host wall time at some point
// in time. KVM has a hypercall that asks the VMM to populate this structure and
// it's actually used, which is rather puzzling considering that PV clock
// provides an API to get wall time at the time of boot and offset from that time
// which seem to be enough.
//
// More detailed description of KVM API is available here:
//  https://www.kernel.org/doc/Documentation/virtual/kvm/hypercalls.txt
struct PvClockOffset {
  uint64_t sec;
  uint64_t nsec;
  uint64_t tsc;
  uint32_t flags;
  uint32_t unused[9];
} __PACKED;

// Updates guest boot time.
zx_status_t pvclock_update_boot_time(hypervisor::GuestPhysicalAddressSpace* gpas,
                                     zx_vaddr_t guest_paddr);

// Remembers guest physical address for KVM clock system time structure and enables updates
// to guest system time.
zx_status_t pvclock_reset_clock(PvClockState* pvclock, hypervisor::GuestPhysicalAddressSpace* gpas,
                                zx_vaddr_t guest_paddr);

// Disables updates to guest system time.
void pvclock_stop_clock(PvClockState* pvclock);

// Updates guest system time. If updates disabled does nothing.
void pvclock_update_system_time(PvClockState* pvclock, hypervisor::GuestPhysicalAddressSpace* gpas);

// Populates mapping between TSC and wall time per guest request. guest_padds contains
// physical address of PvClockOffset structure where the result should be stored.
zx_status_t pvclock_populate_offset(hypervisor::GuestPhysicalAddressSpace* gpas,
                                    zx_vaddr_t guest_paddr);

#endif  // ZIRCON_KERNEL_ARCH_X86_HYPERVISOR_PVCLOCK_PRIV_H_
