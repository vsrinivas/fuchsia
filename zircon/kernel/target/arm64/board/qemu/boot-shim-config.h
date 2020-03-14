// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 1
#define USE_DEVICE_TREE_CPU_COUNT 1
#define USE_DEVICE_TREE_GIC_VERSION 1
#define PRINT_DEVICE_TREE 0
// leave the kernel in place where the ZBI was placed to save some boot
// time on KVM hosted qemu machines
#define REMOVE_KERNEL_FROM_ZBI 0

#define MAX_CPU_COUNT 16
static size_t cpu_count = 0;

static const zbi_mem_range_t mem_config[] = {
    // ZBI_MEM_RANGE_RAM will come from device tree
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        .paddr = 0,
        .length = 0x40000000,
    },
};

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0x09000000,
    .irq = 33,
};

static const dcfg_arm_gicv3_driver_t gicv3_driver = {
    .mmio_phys = 0x08000000,
    .gicd_offset = 0x00000,
    .gicr_offset = 0xa0000,
    .gicr_stride = 0x20000,
    .ipi_base = 12,
    .optional = true,
};

static const dcfg_arm_gicv2_driver_t gicv2_driver = {
    .mmio_phys = 0x08000000,
    .msi_frame_phys = 0x08020000,
    .gicd_offset = 0x00000,
    .gicc_offset = 0x10000,
    .ipi_base = 12,
    .optional = true,
    .use_msi = true,
};

static const dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = true,
};

static const dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30,
    .irq_virt = 27,
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_QEMU,
    .pid = PDEV_PID_QEMU,
    .board_name = "qemu",
};

static int saved_gic_version = -1;

static void set_gic_version(int gic_version) { saved_gic_version = gic_version; }

static void add_cpu_topology(zbi_header_t* zbi) {
  zbi_topology_node_t nodes[MAX_CPU_COUNT];

  // clamp to the max cpu
  if (cpu_count > MAX_CPU_COUNT) {
    cpu_count = MAX_CPU_COUNT;
  }

  for (size_t index = 0; index < cpu_count; index++) {
    nodes[index] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {.processor = {.logical_ids = {index},
                                 .logical_id_count = 1,
                                 .flags = (index == 0) ? ZBI_TOPOLOGY_PROCESSOR_PRIMARY : 0,
                                 .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                                 .architecture_info = {
                                     .arm = {
                                         // qemu seems to put 16 cores per aff0 level, max 32 cores.
                                         .cluster_1_id = (index / 16),
                                         .cpu_id = (index % 16),
                                         .gic_id = index,
                                     }}}}};
  }

  append_boot_item(zbi, ZBI_TYPE_CPU_TOPOLOGY,
                   sizeof(zbi_topology_node_t),  // Extra
                   &nodes, sizeof(zbi_topology_node_t) * cpu_count);
}

static void append_board_boot_item(zbi_header_t* bootdata) {
  add_cpu_topology(bootdata);

  // add memory configuration
  append_boot_item(bootdata, ZBI_TYPE_MEM_CONFIG, 0, &mem_config,
                   sizeof(zbi_mem_range_t) * countof(mem_config));

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_PL011_UART, &uart_driver,
                   sizeof(uart_driver));

  // append the gic information from the specific gic version we detected from the
  // device tree.
  if (saved_gic_version == 2) {
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &gicv2_driver,
                     sizeof(gicv2_driver));
  } else if (saved_gic_version >= 3) {
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V3, &gicv3_driver,
                     sizeof(gicv3_driver));
  } else {
    fail("failed to detect gic version from device tree\n");
  }

  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GENERIC_TIMER, &timer_driver,
                   sizeof(timer_driver));

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}

static void set_cpu_count(uint32_t new_count) {
  if (new_count > 0) {
    cpu_count = new_count;
  }
}
