// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_TARGET_ARM64_BOARD_QEMU_BOOT_SHIM_CONFIG_H_
#define ZIRCON_KERNEL_TARGET_ARM64_BOARD_QEMU_BOOT_SHIM_CONFIG_H_

#define HAS_DEVICE_TREE 1
#define USE_DEVICE_TREE_CPU_COUNT 1
#define USE_DEVICE_TREE_GIC_VERSION 1
#define USE_DEVICE_TREE_TOP_OF_RAM 1
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

static const zbi_dcfg_simple_t uart_driver = {
    .mmio_phys = 0x09000000,
    .irq = 33,
};

static const zbi_dcfg_arm_gic_v3_driver_t gic_v3_driver = {
    .mmio_phys = 0x08000000,
    .gicd_offset = 0x00000,
    .gicr_offset = 0xa0000,
    .gicr_stride = 0x20000,
    .ipi_base = 0,
    .optional = true,
};

static const zbi_dcfg_arm_gic_v2_driver_t gic_v2_driver = {
    .mmio_phys = 0x08000000,
    .msi_frame_phys = 0x08020000,
    .gicd_offset = 0x00000,
    .gicc_offset = 0x10000,
    .ipi_base = 0,
    .optional = true,
    .use_msi = true,
};

static const zbi_dcfg_arm_psci_driver_t psci_driver = {
    .use_hvc = true,
};

static const zbi_dcfg_arm_generic_timer_driver_t timer_driver = {
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

  // clang-format off
  for (size_t index = 0; index < cpu_count; index++) {
    nodes[index] = (zbi_topology_node_t){
        .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
        .parent_index = ZBI_TOPOLOGY_NO_PARENT,
        .entity = {
          .processor = {
            .logical_ids = {(uint16_t)index},
            .logical_id_count = 1,
            .flags = (index == 0) ? ZBI_TOPOLOGY_PROCESSOR_PRIMARY : 0,
            .architecture = ZBI_TOPOLOGY_ARCH_ARM,
            .architecture_info = {
              .arm = {
                // qemu seems to put 16 cores per aff0 level, max 32 cores.
                .cluster_1_id = (uint8_t)(index / 16),
                .cpu_id = (index % 16),
                .gic_id = (uint8_t)(index),
              }
            }
          }
        }
    };
  }
  // clang-format on

  const uint32_t length = (uint32_t)(sizeof(zbi_topology_node_t) * cpu_count);
  append_boot_item(zbi, ZBI_TYPE_CPU_TOPOLOGY,
                   sizeof(zbi_topology_node_t),  // Extra
                   &nodes, length);
}

static uint64_t top_of_ram;

static void set_top_of_ram(uint64_t top) {
  if (top > top_of_ram) {
    top_of_ram = top;
  }
}

static void append_board_boot_item(zbi_header_t* bootdata) {
  add_cpu_topology(bootdata);

  // add some "nvram" for storing the crashlog
  const uint64_t crashlog_length = 0x10000;
  const zbi_nvram_t crashlog = {
      .base = top_of_ram - crashlog_length,
      .length = crashlog_length,
  };
  append_boot_item(bootdata, ZBI_TYPE_NVRAM, 0, &crashlog, sizeof(crashlog));

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_PL011_UART, &uart_driver,
                   sizeof(uart_driver));

  // append the gic information from the specific gic version we detected from the
  // device tree.
  if (saved_gic_version == 2) {
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GIC_V2, &gic_v2_driver,
                     sizeof(gic_v2_driver));
  } else if (saved_gic_version >= 3) {
    append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GIC_V3, &gic_v3_driver,
                     sizeof(gic_v3_driver));
  } else {
    fail("failed to detect gic version from device tree\n");
  }

  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_PSCI, &psci_driver,
                   sizeof(psci_driver));
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, ZBI_KERNEL_DRIVER_ARM_GENERIC_TIMER,
                   &timer_driver, sizeof(timer_driver));

  // add platform ID
  append_boot_item(bootdata, ZBI_TYPE_PLATFORM_ID, 0, &platform_id, sizeof(platform_id));

  // add fake serial number
  const char serial_number[] = "fake0123456789";
  append_boot_item(bootdata, ZBI_TYPE_SERIAL_NUMBER, 0, &serial_number, sizeof(serial_number) - 1);
}

static void set_cpu_count(uint32_t new_count) {
  if (new_count > 0) {
    cpu_count = new_count;
  }
}

#endif  // ZIRCON_KERNEL_TARGET_ARM64_BOARD_QEMU_BOOT_SHIM_CONFIG_H_
