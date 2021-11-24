// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_PHYS_INCLUDE_PHYS_ARCH_ARCH_HANDOFF_H_
#define ZIRCON_KERNEL_ARCH_ARM64_PHYS_INCLUDE_PHYS_ARCH_ARCH_HANDOFF_H_

#include <zircon/boot/driver-config.h>

#include <ktl/optional.h>

// This holds (or points to) all arm64-specific data that is handed off from
// physboot to the kernel proper at boot time.
//
// TODO(fxbug.dev/88059): Populate me.
struct ArchPhysHandoff {
  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI) payload.
  ktl::optional<dcfg_arm_psci_driver_t> psci_driver;
};

#endif  // ZIRCON_KERNEL_ARCH_ARM64_PHYS_INCLUDE_PHYS_ARCH_ARCH_HANDOFF_H_
