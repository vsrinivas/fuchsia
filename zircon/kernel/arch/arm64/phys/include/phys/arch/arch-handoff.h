// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_PHYS_INCLUDE_PHYS_ARCH_ARCH_HANDOFF_H_
#define ZIRCON_KERNEL_ARCH_ARM64_PHYS_INCLUDE_PHYS_ARCH_ARCH_HANDOFF_H_

#include <zircon/boot/driver-config.h>

#include <ktl/optional.h>
#include <ktl/variant.h>

// This holds (or points to) all arm64-specific data that is handed off from
// physboot to the kernel proper at boot time.
struct ArchPhysHandoff {
  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_AMLOGIC_HDCP) payload.
  ktl::optional<dcfg_amlogic_hdcp_driver_t> amlogic_hdcp_driver;

  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_AMLOGIC_RNG) payload.
  ktl::optional<dcfg_amlogic_rng_driver_t> amlogic_rng_driver;

  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER) payload.
  ktl::optional<dcfg_arm_generic_timer_driver_t> generic_timer_driver;

  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2/KDRV_ARM_GIC_V3) payload.
  ktl::variant<ktl::monostate, dcfg_arm_gicv2_driver_t, dcfg_arm_gicv3_driver_t> gic_driver;

  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI) payload.
  ktl::optional<dcfg_arm_psci_driver_t> psci_driver;

  // (ZBI_TYPE_KERNEL_DRIVER, KDRV_GENERIC_32BIT_WATCHDOG) payload.
  ktl::optional<dcfg_generic_32bit_watchdog_t> generic_32bit_watchdog_driver;
};

#endif  // ZIRCON_KERNEL_ARCH_ARM64_PHYS_INCLUDE_PHYS_ARCH_ARCH_HANDOFF_H_
