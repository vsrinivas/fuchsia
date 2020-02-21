// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 1

#define WDT_MODE 0x10007000
#define WDT_MODE_EN (1 << 0)
#define WDT_MODE_KEY 0x22000000

static void disable_watchdog(void) {
  volatile uint32_t* wdt_mode = (uint32_t*)WDT_MODE;
  uint32_t tmp = *wdt_mode;
  tmp &= ~WDT_MODE_EN;
  tmp |= WDT_MODE_KEY;
  *wdt_mode = tmp;
}

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
        .paddr = 0x40000000,
        .length = 0x40000000,  // 1GB
    },
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        // This is not the entire peripheral range, but enough to cover what we use in the kernel.
        .paddr = 0x10000000,
        .length = 0x01200000,
    },
    {
        // Memory to reserve to avoid stomping on bootloader data.
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x40000000,
        .length = 0x80000,
    },
    {
        // mt8167-atf-reserved-memory
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x43000000,
        .length = 0x30000,
    },
    {
        // ram_console
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x44400000,
        .length = 0x10000,
    },
    {
        // pstore
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x44410000,
        .length = 0xe0000,
    },
    {
        // minirdump
        .type = ZBI_MEM_RANGE_RESERVED,
        .paddr = 0x444f0000,
        .length = 0x10000,
    },
};

static const dcfg_soc_uart_t uart_driver = {
    .soc_mmio_phys = 0x10200000,
    .uart_mmio_phys = 0x11005000,
    .irq = 84 + 32,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0x10310000,
    .gicd_offset = 0x00000,
    .gicc_offset = 0x1f000,
    .gich_offset = 0x30000,
    .gicv_offset = 0x50000,
    .ipi_base = 5,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 16 + 14,  // PHYS_NONSECURE_PPI: GIC_PPI 14
    .irq_virt = 16 + 11,  // VIRT_PPI: GIC_PPI 11
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_CLEO,
    .board_name = "cleo",
};

static void append_board_boot_item(zbi_header_t* bootdata) {
  // Disable watchdog timer for now.
  // TODO - remove this after we have a proper watchdog driver in userspace.
  disable_watchdog();

  // add CPU configuration
  append_boot_item(bootdata, ZBI_TYPE_CPU_CONFIG, 0, &cpu_config,
                   sizeof(zbi_cpu_config_t) + sizeof(zbi_cpu_cluster_t) * cpu_config.cluster_count);

  // add memory configuration
  append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_config,
                   sizeof(zbi_mem_range_t) * countof(mem_config));

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_MT8167_UART, &uart_driver,
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
