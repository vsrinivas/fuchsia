// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 1

static const zbi_cpu_config_t cpu_config = {
    .cluster_count = 2,
    .clusters =
        {
            {
                .cpu_count = 4,
            },
            {
                .cpu_count = 4,
            },
        },
};

static const zbi_mem_range_t mem_config[] = {
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr = 0x80000000,
        .length = 0x100000000,  // 4 gig
    },
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        .paddr = 0x00000000,
        .length = 0x40000000,
    },
    {
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x85800000,
        .length = 0xEF00000,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0x0c1b0000,
    .irq = 146,
};

static const dcfg_arm_gicv3_driver_t gicv3_driver = {
    .mmio_phys = 0x17a00000,
    .gicd_offset = 0x000000,
    .gicr_offset = 0x100000,
    .gicr_stride = 0x20000,
    .ipi_base = 5,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

/*
static const dcfg_msm_power_driver_t power_driver = {
    .soc_imem_phys = 0x8600000,
    .soc_imem_offset = 0x65c,
};
*/

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_virt = 19,
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_QUALCOMM,
    .pid = PDEV_PID_QUALCOMM_MSM8998,
    .board_name = "msm8998",
};

static void append_board_boot_item(zbi_header_t* bootdata) {
  // add CPU configuration
  append_boot_item(bootdata, ZBI_TYPE_CPU_CONFIG, 0, &cpu_config,
                   sizeof(zbi_cpu_config_t) + sizeof(zbi_cpu_cluster_t) * cpu_config.cluster_count);

  // add memory configuration
  append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_config,
                   sizeof(zbi_mem_range_t) * countof(mem_config));

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_MSM_UART, &uart_driver,
                   sizeof(uart_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V3, &gicv3_driver,
                   sizeof(gicv3_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                   sizeof(timer_driver));
  //    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_MSM_POWER, &power_driver,
  //                    sizeof(power_driver));

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
