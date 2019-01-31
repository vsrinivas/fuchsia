// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define HAS_DEVICE_TREE 1

static const zbi_cpu_config_t cpu_config = {
// TODO(voydanoff) enable second cluster
#if 0
    .cluster_count = 2,
#else
    .cluster_count = 1,
#endif
    .clusters = {
        {
            .cpu_count = 2,
        },
        {
            .cpu_count = 4,
        },
    },
};

static const zbi_mem_range_t mem_config[] = {
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr = 0,
        .length = 0x80000000, // 2GB
    },
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        .paddr = 0xff000000,
        .length = 0x01000000,
    },
    // linux,secmon
    {
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x05000000,
        .length = 0x2400000,
    },
/* Linux device tree already excludes this region
    // linux,meson-fb
    {
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x7f800000,
        .length = 0x800000,
    },
*/
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0xff803000,
    .irq = 225,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0xffc00000,
    .gicd_offset = 0x1000,
    .gicc_offset = 0x2000,
    .ipi_base = 5,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
    .reboot_args = { 1, 0, 0 },
    .reboot_bootloader_args = { 4, 0, 0 },
    .reboot_recovery_args = { 2, 0, 0 },
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 16 + 14,    // PHYS_NONSECURE_PPI: GIC_PPI 14
    .irq_virt = 16 + 11,    // VIRT_PPI: GIC_PPI 11
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_SHERLOCK,
    .board_name = "sherlock",
};

static void append_board_boot_item(zbi_header_t* bootdata) {
    // add CPU configuration
    append_boot_item(bootdata, ZBI_TYPE_CPU_CONFIG, 0, &cpu_config,
                    sizeof(zbi_cpu_config_t) +
                    sizeof(zbi_cpu_cluster_t) * cpu_config.cluster_count);

    // add memory configuration
    append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_config,
                    sizeof(zbi_mem_range_t) * countof(mem_config));

    // add kernel drivers
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_AMLOGIC_UART, &uart_driver,
                     sizeof(uart_driver));
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                     sizeof(gicv2_driver));
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                     sizeof(psci_driver));
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                     sizeof(timer_driver));

    // add platform ID
    append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
