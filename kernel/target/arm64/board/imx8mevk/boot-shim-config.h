// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

static const bootdata_cpu_config_t cpu_config = {
    .cluster_count = 1,
    .clusters = {
        {
            .cpu_count = 4,
        },
    },
};

static const bootdata_mem_range_t mem_config[] = {
    {
        .type = BOOTDATA_MEM_RANGE_RAM,
        .paddr = 0x40000000,
        .length = 0xc0000000, // 3GB
    },
    {
        .type = BOOTDATA_MEM_RANGE_PERIPHERAL,
        .paddr = 0,
        .length = 0x40000000,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0x30860000,
    .irq = 58,
};

static const dcfg_arm_gicv3_driver_t gicv3_driver = {
    .mmio_phys = 0x38800000,
    .gicd_offset = 0x00000,
    .gicr_offset = 0x80000,
    .gicr_stride = 0x20000,
    .ipi_base = 9,
    // Used for Errata e11171
    .mx8_gpr_phys = 0x30340000,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30,
    .irq_virt = 27,
    .freq_override = 8333333,
};

static const bootdata_platform_id_t platform_id = {
    .vid = PDEV_VID_NXP,
    .pid = PDEV_PID_IMX8MEVK,
    .board_name = "imx8mevk",
};

static void append_board_bootdata(bootdata_t* bootdata) {
    // add CPU configuration
    append_bootdata(bootdata, BOOTDATA_CPU_CONFIG, 0, &cpu_config,
                    sizeof(bootdata_cpu_config_t) +
                    sizeof(bootdata_cpu_cluster_t) * cpu_config.cluster_count);

    // add memory configuration
    append_bootdata(bootdata, BOOTDATA_MEM_CONFIG, 0, &mem_config,
                    sizeof(bootdata_mem_range_t) * countof(mem_config));

    // add kernel drivers
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_NXP_IMX_UART, &uart_driver,
                    sizeof(uart_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GIC_V3, &gicv3_driver,
                    sizeof(gicv3_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                    sizeof(psci_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                    sizeof(timer_driver));

    // add platform ID
    append_bootdata(bootdata, BOOTDATA_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
