// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 0
#define PRINT_DEVICE_TREE 0
#define PRINT_ZBI 1
#define DEBUG_UART 1

static const zbi_cpu_config_t cpu_config = {
    .cluster_count = 1,
    .clusters =
        {
            {
                .cpu_count = 4,
            },
        },
};

static const zbi_mem_range_t mem_config[] = {
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr =  0x00000000,
        .length = 0x40000000,	// for 1GB version
    },
    {
        // bl31 (trusted-firmware data)
        // for psci functions
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr =  0x00000000,
        .length = 0x00010000,
    },
    {
        // GPU memory
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr =  0x3c000000,
        .length = 0x04000000,	// default 64M
    },
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        .paddr = 0xfc000000,
        .length = 0x4000000,
    },
};

static const dcfg_simple_t mini_uart_driver = {
    .mmio_phys = 0xfe215040,
    .irq = 29,
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0xfe201000,
    .irq = 57,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0xff840000,
    .gicd_offset = 0x1000,
    .gicc_offset = 0x2000,
    .gich_offset = 0x4000,
    .gicv_offset = 0x6000,
    .ipi_base = 12,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30,
    .irq_virt = 27,
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_BROADCOM,
    .pid = PDEV_PID_BCM2711,
    .board_name = "rpi4",
};

static const zbi_board_info_t board_info = {
	.revision = 0xa03111,
};

static void append_board_boot_item(zbi_header_t* bootdata) {
  // add CPU configuration
  append_boot_item(bootdata, ZBI_TYPE_CPU_CONFIG, 0, &cpu_config,
                   sizeof(zbi_cpu_config_t) + sizeof(zbi_cpu_cluster_t) * cpu_config.cluster_count);

  // add memory configuration
  append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_config,
                   sizeof(zbi_mem_range_t) * countof(mem_config));

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_DW8250_UART, &mini_uart_driver,
                   sizeof(mini_uart_driver));
  //append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_PL011_UART, &uart_driver,
  //                 sizeof(uart_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                   sizeof(gicv2_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                   sizeof(timer_driver));

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
  // add board info
  append_boot_item(bootdata, ZBI_TYPE_DRV_BOARD_INFO, 0, &board_info, sizeof(board_info));  
}
