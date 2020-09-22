// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/power.h>
#include <dev/psci.h>
#include <pdev/driver.h>
#include <pdev/power.h>

static constexpr paddr_t kDwWdt0Cr = 0xf7e8'0400;
static constexpr paddr_t kDwWdt0Crr = 0xf7e8'040c;

static constexpr uint32_t kDwWdtCrRpl8Pclk = 0x08;
static constexpr uint32_t kDwWdtCrEnable = 0x01;

static constexpr uint32_t kDwWdtCrrRestartValue = 0x76;

static void as370_reboot(enum reboot_flags flags) {
  // TODO(fxbug.dev/34426): Handle REBOOT_BOOTLOADER and REBOOT_RECOVERY cases.
  writel(kDwWdtCrEnable | kDwWdtCrRpl8Pclk, periph_paddr_to_vaddr(kDwWdt0Cr));
  writel(kDwWdtCrrRestartValue, periph_paddr_to_vaddr(kDwWdt0Crr));
}

static void as370_shutdown() {
  // TODO(fxbug.dev/34477): Make this work.
  psci_system_off();
}

static const struct pdev_power_ops as370_power_ops = {
    .reboot = as370_reboot,
    .shutdown = as370_shutdown,
};

static void as370_power_init(const void* driver_data, uint32_t length) {
  pdev_register_power(&as370_power_ops);
}

LK_PDEV_INIT(as370_power_init, KDRV_AS370_POWER, as370_power_init, LK_INIT_LEVEL_PLATFORM)
