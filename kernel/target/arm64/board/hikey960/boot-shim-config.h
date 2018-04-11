// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define HAS_DEVICE_TREE 1

static const bootdata_cpu_config_t cpu_config = {
    .cluster_count = 2,
    .clusters = {
        {
            .cpu_count = 4,
        },
        {
            .cpu_count = 4,
        },
    },
};

static const bootdata_mem_range_t mem_config[] = {
    {
        .type = BOOTDATA_MEM_RANGE_RAM,
        .length = 0xc0000000, // 3GB
    },
    {
        .type = BOOTDATA_MEM_RANGE_PERIPHERAL,
        .paddr = 0xe8100000,
        .length = 0x17f00000,
    },
    {
        // memory to reserve to avoid stomping on bootloader data
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x00000000,
        .length = 0x00080000,
    },
    {
        // bl31
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x20200000,
        .length = 0x200000,
    },
    {
        // pstore
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x20a00000,
        .length = 0x100000,
    },
    {
        // lpmx-core
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x89b80000,
        .length = 0x100000,
    },
    {
        // lpmcu
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x89c80000,
        .length = 0x40000,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0xfff32000,
    .irq = 111,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0xe82b0000,
    .gicd_offset = 0x1000,
    .gicc_offset = 0x2000,
    .gich_offset = 0x4000,
    .gicv_offset = 0x6000,
    .ipi_base = 13,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30,
    .irq_virt = 27,
};

static const dcfg_hisilicon_power_driver_t power_driver = {
    .sctrl_phys = 0xfff0a000,
    .pmu_phys = 0xfff34000,
};

static const bootdata_platform_id_t platform_id = {
    .vid = PDEV_VID_96BOARDS,
    .pid = PDEV_PID_HIKEY960,
    .board_name = "hikey960",
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
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_PL011_UART, &uart_driver,
                    sizeof(uart_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                    sizeof(gicv2_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                    sizeof(psci_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                    sizeof(timer_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_HISILICON_POWER, &power_driver,
                    sizeof(power_driver));

    // add platform ID
    append_bootdata(bootdata, BOOTDATA_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
