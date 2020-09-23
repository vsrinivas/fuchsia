// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 0

static const zbi_mem_range_t mem_config[] = {
    {
        .paddr = 0x02000000,
        .length = 0x20000000,  // 512M
        .type = ZBI_MEM_RANGE_RAM,
    },
    {
        .paddr = 0xf0000000,
        .length = 0x10000000,
        .type = ZBI_MEM_RANGE_PERIPHERAL,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0xf7e80c00,
    .irq = 88,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0xf7900000,
    .gicd_offset = 0x1000,
    .gicc_offset = 0x2000,
    .ipi_base = 9,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30, .irq_virt = 27,
    //.freq_override = 8333333,
};

static const zbi_platform_id_t platform_id = {
    PDEV_VID_GOOGLE,
    PDEV_PID_VISALIA,
    "visalia",
};

static void add_cpu_topology(zbi_header_t* zbi) {
#define TOPOLOGY_CPU_COUNT 4
  zbi_topology_node_t nodes[TOPOLOGY_CPU_COUNT];

  for (uint8_t index = 0; index < TOPOLOGY_CPU_COUNT; index++) {
    nodes[index] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity =
            {
                .processor =
                    {
                        .logical_ids = {index},
                        .logical_id_count = 1,
                        .flags = (uint16_t)(index == 0 ? ZBI_TOPOLOGY_PROCESSOR_PRIMARY : 0),
                        .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                        .architecture_info =
                            {
                                .arm =
                                    {
                                        .cpu_id = index,
                                        .gic_id = index,
                                    },
                            },
                    },
            },
    };
  }

  append_boot_item(zbi, ZBI_TYPE_CPU_TOPOLOGY, sizeof(zbi_topology_node_t), &nodes,
                   sizeof(zbi_topology_node_t) * TOPOLOGY_CPU_COUNT);
}

static void append_board_boot_item(zbi_header_t* bootdata) {
  add_cpu_topology(bootdata);

  // add memory configuration
  append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_config,
                   sizeof(zbi_mem_range_t) * countof(mem_config));

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_DW8250_UART, &uart_driver,
                   sizeof(uart_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                   sizeof(gicv2_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                   sizeof(timer_driver));

  // append_boot_item doesn't support zero-length payloads, so we have to call zbi_create_entry
  // directly.
  uint8_t* new_section = NULL;
  zbi_result_t result = zbi_create_entry(bootdata, SIZE_MAX, ZBI_TYPE_KERNEL_DRIVER,
                                         KDRV_AS370_POWER, 0, 0, (void**)&new_section);
  if (result != ZBI_RESULT_OK) {
    fail("zbi_create_entry failed\n");
  }

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}
