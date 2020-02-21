// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 1

// hack to relocate the kernel and zbi to a fixed address
#define RELOCATE_KERNEL 1
#define RELOCATE_KERNEL_ADDRESS (0xa0000000)
#define RELOCATE_ZBI_ADDRESS (0xb0000000)

static const zbi_cpu_config_t cpu_config = {
    .cluster_count = 1,  // enable both clusters when clock tree can upclock cluster 1
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
        //.paddr = 0x80000000,
        //.length = 0x80000000, // 2GB
        .paddr = 0xa0000000,  // workaround for something dangerous about 0x80000000 memory region
        .length = 0x80000000 - 0x20000000,  // <2GB
    },
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        // This is not the entire peripheral range, but enough to cover what we use in the kernel.
        .paddr = 0x00000000,
        .length = 0x10000000,
    },
    {
        // other_ext_mem: other_ext_region@0
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x85b00000,
        .length = 0xd00000,
    },
    {
        // modem_mem: modem_region@0
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x86c00000,
        .length = 0x6a00000,
    },
    {
        // adsp_fw_mem: adsp_fw_region@0
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x8d600000,
        .length = 0x1100000,
    },
    {
        // wcnss_fw_mem: wcnss_fw_region@0
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x8e700000,
        .length = 0x700000,
    },
    {
        // dfps_data_mem: dfps_data_mem@0
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x90000000,
        .length = 0x1000,
    },
    {
        // cont_splash_mem: splash_region@0
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x90001000,
        .length = 0x13ff000,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0x078af000,
    .irq = 107 + 32,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0x0b000000,
    .gicd_offset = 0x00000,
    .gicc_offset = 0x2000,
    .ipi_base = 5,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const dcfg_msm_power_driver_t power_driver = {
    .soc_imem_phys = 0x8600000,
    .soc_imem_offset = 0x65c,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_virt = 16 + 4,  // VIRT_PPI: GIC_PPI 4
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_QUALCOMM,
    .pid = PDEV_PID_QUALCOMM_MSM8X53,
    .board_name = "msm8x53-som",
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
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                   sizeof(gicv2_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                   sizeof(timer_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_MSM_POWER, &power_driver,
                   sizeof(power_driver));

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
