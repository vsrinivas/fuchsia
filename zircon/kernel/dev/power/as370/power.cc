// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <lib/arch/intrin.h>
#include <reg.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/power.h>
#include <dev/power/as370/init.h>
#include <dev/psci.h>
#include <pdev/power.h>

#define LOCAL_TRACE 0

// Quick driver that attempts to use watchdog 0 to reset the system

static constexpr paddr_t kDwWdt0Cr = 0xf7e8'0400;
static constexpr paddr_t kDwWdt0Torr = 0xf7e8'0404;
static constexpr paddr_t kDwWdt0Crr = 0xf7e8'040c;

// set to 8 clocks per tick, enable
static constexpr uint32_t kDwWdtCrRpl8Pclk = 0x08;
static constexpr uint32_t kDwWdtCrEnable = 0x01;

// set to a very short timeout
static constexpr uint32_t kDwDdtTorrInitValue = 0;

// used to kick the watchdog
static constexpr uint32_t kDwWdtCrrRestartValue = 0x76;

static void as370_reboot(enum reboot_flags flags) {
  auto kWdWdt0Cr_virt = periph_paddr_to_vaddr(kDwWdt0Cr);
  auto kWdWdt0Torr_virt = periph_paddr_to_vaddr(kDwWdt0Torr);
  auto kWdWdt0Crr_virt = periph_paddr_to_vaddr(kDwWdt0Crr);

  LTRACEF("flags %d\n", flags);

  // TODO(fxbug.dev/34426): Handle REBOOT_BOOTLOADER and REBOOT_RECOVERY cases.
  writel(kDwWdtCrEnable | kDwWdtCrRpl8Pclk, kWdWdt0Cr_virt);
  writel(kDwDdtTorrInitValue, kWdWdt0Torr_virt);
  writel(kDwWdtCrrRestartValue, kWdWdt0Crr_virt);

  // spin a little bit to let it take effect
  for (int i = 0; i < 10000000; i++) {
    arch::Yield();
  }

  LTRACEF("failed to reset\n");
}

static void as370_shutdown() {
  // TODO(fxbug.dev/34477): Make this work.
  psci_system_off();
}

static const struct pdev_power_ops as370_power_ops = {
    .reboot = as370_reboot,
    .shutdown = as370_shutdown,
    .cpu_off = psci_cpu_off,
    .cpu_on = psci_cpu_on,
};

void as370_power_init_early() { pdev_register_power(&as370_power_ops); }
