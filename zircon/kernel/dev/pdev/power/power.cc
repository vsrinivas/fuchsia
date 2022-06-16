// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>

#include <dev/power.h>
#include <dev/psci.h>
#include <pdev/power.h>

static const struct pdev_power_ops default_ops = {
    .reboot = psci_system_reset,
    .shutdown = psci_system_off,
    .cpu_off = psci_cpu_off,
    .cpu_on = psci_cpu_on,
};

static const struct pdev_power_ops* power_ops = &default_ops;

void power_reboot(enum reboot_flags flags) { power_ops->reboot(flags); }
void power_shutdown() { power_ops->shutdown(); }
uint32_t power_cpu_off() { return power_ops->cpu_off(); }
uint32_t power_cpu_on(uint64_t mpid, paddr_t entry) { return power_ops->cpu_on(mpid, entry); }

void pdev_register_power(const struct pdev_power_ops* ops) {
  power_ops = ops;
  arch::ThreadMemoryBarrier();
}
