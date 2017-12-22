// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/power.h>
#include <dev/psci.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/driver.h>
#include <pdev/power.h>
#include <stdio.h>

#define PMU_HRST_OFFSET         0x404
#define SCTRL_PEREN1_OFFSET     0x170
#define SCTRL_DDR_BYPASS        (1 << 31)
#define SCTRL_REBOOT_OFFSET     0x4

static uint64_t sctrl_base;
static uint64_t pmu_base;

static void hisi_reboot(enum reboot_flags flags) {
    volatile void* sctrl = (volatile void *)sctrl_base;
    volatile void* pmu = (volatile void *)pmu_base;

    uint32_t temp = readl(pmu + PMU_HRST_OFFSET);
    temp = (temp & 0xFFFFFF00) | (flags == REBOOT_BOOTLOADER ? 1 : 0);
    writel(temp, pmu + PMU_HRST_OFFSET);

    writel(SCTRL_DDR_BYPASS, sctrl + SCTRL_PEREN1_OFFSET);
    writel(0xdeadbeef, sctrl + SCTRL_REBOOT_OFFSET);
}

static void hisi_shutdown(void) {
    printf("SHUTDOWN NOT IMPLEMENTED\n");
}

static const struct pdev_power_ops hisi_power_ops = {
    .reboot = hisi_reboot,
    .shutdown = hisi_shutdown,
};

static void hisi_power_init(mdi_node_ref_t* node, uint level) {
    bool got_sctrl = false;
    bool got_pmu = false;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_HISI_POWER_SCTRL_BASE_VIRT:
            got_sctrl = !mdi_node_uint64(&child, &sctrl_base);
            break;
        case MDI_HISI_POWER_PMU_BASE_VIRT:
            got_pmu = !mdi_node_uint64(&child, &pmu_base);
            break;
        }
    }

    if (!got_sctrl) {
        panic("hisi power: MDI_HISI_POWER_SCTRL_BASE_VIRT not defined\n");
    }
    if (!got_pmu) {
        panic("hisi power uart: MDI_HISI_POWER_PMU_BASE_VIRT not defined\n");
    }

    pdev_register_power(&hisi_power_ops);
}

LK_PDEV_INIT(hisi_power_init, MDI_HISI_POWER, hisi_power_init, LK_INIT_LEVEL_PLATFORM);
