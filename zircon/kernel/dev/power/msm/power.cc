// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/periphmap.h>
#include <dev/power.h>
#include <dev/psci.h>
#include <pdev/driver.h>
#include <pdev/power.h>
#include <zircon/boot/driver-config.h>
#include <stdio.h>

static vaddr_t imem_base;
static vaddr_t imem_offset;

enum RestartReason {
    RESTART_REASON_BOOTLOADER = 0x77665500,
};

static void msm_reboot(enum reboot_flags flags) {
    // Set reboot_reason and handoff to PSCI for the actual reboot.
    if(flags == REBOOT_BOOTLOADER) {
        writel(RESTART_REASON_BOOTLOADER, periph_paddr_to_vaddr(imem_base+imem_offset));
    }
    psci_system_reset(flags);
}

static void msm_shutdown() {
    // Handoff to PSCI
    psci_system_off();
}

static const struct pdev_power_ops msm_power_ops = {
    .reboot = msm_reboot,
    .shutdown = msm_shutdown,
};

static void msm_power_init(const void* driver_data, uint32_t length) {
    ASSERT(length >= sizeof(dcfg_msm_power_driver_t));
    auto driver = static_cast<const dcfg_msm_power_driver_t*>(driver_data);
    ASSERT(driver->soc_imem_phys);


    // get virtual addresses of our peripheral bases
    imem_base = driver->soc_imem_phys;
    imem_offset = driver->soc_imem_offset;
    ASSERT(imem_base);

    pdev_register_power(&msm_power_ops);
}

LK_PDEV_INIT(msm_power_init, KDRV_MSM_POWER, msm_power_init, LK_INIT_LEVEL_PLATFORM)
