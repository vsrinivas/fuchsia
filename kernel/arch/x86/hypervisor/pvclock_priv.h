// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <hypervisor/guest_physical_address_space.h>
#include <zircon/types.h>

static const uint32_t kKvmSystemTimeMsrOld = 0x12;
static const uint32_t kKvmSystemTimeMsr = 0x4b564d01;

static const uint32_t kKvmBootTimeOld = 0x11;
static const uint32_t kKvmBootTime = 0x4b564d00;

static const uint32_t kKvmFeatureClockSourceOld = (1 << 0);
static const uint32_t kKvmFeatureClockSource = (1 << 3);

struct PvClockState;

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
