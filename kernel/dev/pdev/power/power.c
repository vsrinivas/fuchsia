// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_ops.h>
#include <dev/power.h>
#include <dev/psci.h>
#include <pdev/power.h>

static void default_reboot(enum reboot_flags flags) {
    psci_system_reset(flags);
}

static void default_shutdown(void) {
    psci_system_off();
}

static const struct pdev_power_ops default_ops = {
    .reboot = default_reboot,
    .shutdown = default_shutdown,
};

static const struct pdev_power_ops* power_ops = &default_ops;

void power_reboot(enum reboot_flags flags) {
    power_ops->reboot(flags);
}

void power_shutdown(void)
{
    power_ops->shutdown();
}

void pdev_register_power(const struct pdev_power_ops* ops) {
    power_ops = ops;
    smp_mb();
}
