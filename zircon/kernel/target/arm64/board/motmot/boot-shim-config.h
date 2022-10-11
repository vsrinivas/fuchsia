// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_TARGET_ARM64_BOARD_MOTMOT_BOOT_SHIM_CONFIG_H_
#define ZIRCON_KERNEL_TARGET_ARM64_BOARD_MOTMOT_BOOT_SHIM_CONFIG_H_

#define HAS_DEVICE_TREE 1
#define USE_DEVICE_TREE_CPU_COUNT 0
#define USE_DEVICE_TREE_GIC_VERSION 0
#define PRINT_DEVICE_TREE 0
#define PRINT_ZBI 1
// leave the kernel in place where the ZBI was placed to save some boot
// time on KVM hosted qemu machines
#define REMOVE_KERNEL_FROM_ZBI 0

static const zbi_mem_range_t mem_config[] = {
    // TODO: read this from device tree
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr = 0x80000000,
        .length = 0x37600000,
    },
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr = 0xc0000000,
        .length = 0x20000000,
    },
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr = 0xe2500000,
        .length = 0x1db00000,
    },
    {
        .type = ZBI_MEM_RANGE_RAM,
        .paddr = 0x880000000,
        .length = 0x100000000,
    },
    // TODO: find any reserve regions
    {
        .type = ZBI_MEM_RANGE_PERIPHERAL,
        .paddr = 0x10000000,
        .length = 0x40000000,
    },
};

static const zbi_dcfg_simple_t uart_driver = {
    .mmio_phys = 0x10A00000,
    .irq = 634 + 32,  // SPI[634] INTREQ__USI0_UART_PERIC0
};

static const zbi_dcfg_arm_gic_v3_driver_t gic_v3_driver = {
    .mmio_phys = 0x10400000,
    .gicd_offset = 0x00000,
    .gicr_offset = 0x40000,
    .gicr_stride = 0x20000,
    .ipi_base = 0,
};

static const zbi_dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = false,
};

static const zbi_dcfg_arm_generic_timer_driver_t timer_driver = {
    .irq_phys = 30,
    .irq_virt = 27,
    .freq_override = 24000000,
};

// TODO: fxb/86566 implement proper watchdog driver for hardware
#define WDT_CLUSTER0 (0x10060000)
static const zbi_dcfg_generic32_watchdog_t watchdog_driver = {
    .pet_action =
        {
            .addr = WDT_CLUSTER0 + 0x8,  // count register
            .clr_mask = 0xffffffff,
            .set_mask = 0x8000,  // reload counter
        },
    .enable_action = {},
    .disable_action = {},
    .watchdog_period_nsec = ZX_SEC(10),
    .flags = ZBI_KERNEL_DRIVER_GENERIC32_WATCHDOG_FLAG_ENABLED,
    .reserved = 0,
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_MOTMOT,
    .board_name = "motmot",
};

static void add_cluster(zbi_topology_node_t* node, uint8_t performance_class) {
  // clang-format off
  *node = (zbi_topology_node_t) {
    .entity_type = ZBI_TOPOLOGY_ENTITY_CLUSTER,
    .parent_index = ZBI_TOPOLOGY_NO_PARENT,
    .entity = {
      .cluster = {
        .performance_class = performance_class,
      }
    }
  };
  // clang-format on
}

static void add_cpu(zbi_topology_node_t* node, size_t cpu_num, size_t parent_index) {
  // 0, 0x100, 0x200, ...
  uint32_t mpidr = (uint32_t)cpu_num << 8;

  // clang-format off
  *node = (zbi_topology_node_t) {
    .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
    .parent_index = (uint16_t)parent_index,
    .entity = {
      .processor = {
        .logical_ids = { (uint16_t)cpu_num },
        .logical_id_count = 1,
        .flags = (cpu_num == 0) ? ZBI_TOPOLOGY_PROCESSOR_PRIMARY : 0,
        .architecture = ZBI_TOPOLOGY_ARCH_ARM,
        .architecture_info = {
          .arm = {
            .cluster_1_id = (mpidr >> 8) & 0xff,
            .cpu_id = mpidr & 0xff,
            .gic_id = 0, // unused with GICv3
          }
        }
      }
    }
  };
  // clang-format on
  uart_puts("cpu mpidr ");
  uart_print_hex(mpidr);
  uart_puts("\n");
}

static void add_cpu_topology(zbi_header_t* zbi) {
  zbi_topology_node_t nodes[3 + 8];
  size_t index = 0;
  size_t cpu_num = 0;

  memset(nodes, 0, sizeof(nodes));

  // cpus 0-7 have 0x[0-7]00 mpidr

  // fill in three clusters
  // little cluster, 4 cpus
  size_t little_index = index;
  add_cluster(&nodes[index++], 0x40);

  for (cpu_num = 0; cpu_num < 4; cpu_num++) {
    add_cpu(&nodes[index++], cpu_num, little_index);
  }

  // medium cluster, 2 cpus
  size_t med_index = index;
  add_cluster(&nodes[index++], 0xc0);

  for (cpu_num = 4; cpu_num < 6; cpu_num++) {
    add_cpu(&nodes[index++], cpu_num, med_index);
  }

  // big cluster, 2 cpus
  size_t big_index = index;
  add_cluster(&nodes[index++], 0xff);

  for (cpu_num = 6; cpu_num < 8; cpu_num++) {
    add_cpu(&nodes[index++], cpu_num, big_index);
  }

  append_boot_item(zbi, ZBI_TYPE_CPU_TOPOLOGY, sizeof(zbi_topology_node_t), &nodes,
                   sizeof(zbi_topology_node_t) * countof(nodes));
}

static void append_board_boot_item(zbi_header_t* bootdata) {
  add_cpu_topology(bootdata);

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_MOTMOT_UART, &uart_driver,
                   sizeof(uart_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GIC_V3, &gic_v3_driver,
                   sizeof(gic_v3_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GENERIC_TIMER,
                   &timer_driver, sizeof(timer_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_GENERIC32_WATCHDOG,
                   &watchdog_driver, sizeof(watchdog_driver));
  uint32_t junk = 0;
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_MOTMOT_POWER, &junk, 0);

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));
}

#endif  // ZIRCON_KERNEL_TARGET_ARM64_BOARD_MOTMOT_BOOT_SHIM_CONFIG_H_
