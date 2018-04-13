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
        .length = 0x80000000, // 2GB
    },
    {
        .type = BOOTDATA_MEM_RANGE_PERIPHERAL,
        .paddr = 0xc0000000,
        .length = 0x20000000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0,
        .length = 0x001000000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x10000000,
        .length = 0x00200000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x05100000,
        .length = 0x2000000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x7300000,
        .length = 0x100000,
    },
    {
        .type = BOOTDATA_MEM_RANGE_RESERVED,
        .paddr = 0x75000000,
        .length = 0x9000000,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0xc81004c0,
    .irq = 225,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0xc4300000,
    .gicd_offset = 0x1000,
    .gicc_offset = 0x2000,
    .gich_offset = 0x4000,
    .gicv_offset = 0x6000,
    .ipi_base = 5,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
    .reboot_args = { 1, 0, 0 },
    .reboot_bootloader_args = { 4, 0, 0 },
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30,
};

static const dcfg_amlogic_hdcp_driver_t hdcp_driver = {
    .preset_phys = 0xc1104000,
    .hiu_phys = 0xc883c000,
    .hdmitx_phys = 0xc883a000,
};

static const bootdata_platform_id_t platform_id = {
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .board_name = "vim2",
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
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_AMLOGIC_UART, &uart_driver,
                    sizeof(uart_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                    sizeof(gicv2_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                    sizeof(psci_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                    sizeof(timer_driver));
    append_bootdata(bootdata, BOOTDATA_KERNEL_DRIVER, KDRV_AMLOGIC_HDCP, &hdcp_driver,
                    sizeof(hdcp_driver));


    // add platform ID
    append_bootdata(bootdata, BOOTDATA_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
