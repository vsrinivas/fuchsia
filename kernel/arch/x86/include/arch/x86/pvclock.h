// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

static constexpr uint32_t kKvmSystemTimeMsrOld = 0x12;
static constexpr uint32_t kKvmSystemTimeMsr = 0x4b564d01;

static constexpr uint32_t kKvmBootTimeOld = 0x11;
static constexpr uint32_t kKvmBootTime = 0x4b564d00;

static constexpr uint32_t kKvmFeatureClockSourceOld = 1 << 0;
static constexpr uint32_t kKvmFeatureClockSource = 1 << 3;

// Both structures below are part of the ABI used by Xen and KVM, this ABI is not
// defined by use we just follow it. For more detail please refer to the
// documentation (https://www.kernel.org/doc/Documentation/virtual/kvm/msr.txt).
struct pvclock_boot_time {
    // With multiple VCPUs it is possible that one VCPU can try to read boot time
    // while we are updating it because another VCPU asked for the update. In this
    // case odd version value serves as an indicator for the guest that update is
    // in progress. Therefore we need to update version before we write anything
    // else and after, also we need to user proper memory barriers. The same logic
    // applies to system time version below, even though system time is per VCPU
    // others VCPUs still can access system times of other VCPUs (Linux however
    // never does that).
    uint32_t version;
    uint32_t seconds;
    uint32_t nseconds;
} __PACKED;
static_assert(sizeof(struct pvclock_boot_time) == 12, "sizeof(pvclock_boot_time) should be 12");

struct pvclock_system_time {
    uint32_t version;
    uint32_t pad0;
    uint64_t tsc_timestamp;
    uint64_t system_time;
    uint32_t tsc_mul;
    int8_t tsc_shift;
    uint8_t flags;
    uint8_t pad1[2];
} __PACKED;
static_assert(sizeof(struct pvclock_system_time) == 32, "sizeof(pvclock_system_time) should be 32");

zx_status_t pvclock_init();

bool pvclock_is_present();

uint64_t pvclock_get_tsc_freq();
