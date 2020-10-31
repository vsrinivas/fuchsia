// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#define HAS_DEVICE_TREE 1
#define USE_DEVICE_TREE_CPU_COUNT 1
#define PRINT_DEVICE_TREE 0
// leave the kernel in place where the ZBI was placed to save some boot
// time on KVM hosted qemu machines
#define REMOVE_KERNEL_FROM_ZBI 0

#define MAX_CPU_COUNT 16
static size_t cpu_count = 0;

static const dcfg_simple_t uart_driver = {
    .mmio_phys = 0x10000000,
    .irq = 10,
};

static const dcfg_riscv_plic_driver_t plic_driver = {
    .mmio_phys = 0x0C000000,
    .num_irqs = 127,
};

static const dcfg_riscv_generic_timer_driver_t timer_driver = {
    .freq_hz = 10000000,
};

static const zbi_platform_id_t platform_id = {
    .vid = PDEV_VID_QEMU,
    .pid = PDEV_PID_QEMU,
    .board_name = "qemu",
};

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
                                 .architecture = ZBI_TOPOLOGY_ARCH_RISCV}}};
  }

  append_boot_item(zbi, ZBI_TYPE_CPU_TOPOLOGY,
                   sizeof(zbi_topology_node_t),  // Extra
                   &nodes, sizeof(zbi_topology_node_t) * cpu_count);
}

static void append_board_boot_item(zbi_header_t* bootdata) {
  add_cpu_topology(bootdata);

  // add kernel drivers
  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_NS16550A_UART, &uart_driver,
                   sizeof(uart_driver));

  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_RISCV_PLIC, &plic_driver,
                   sizeof(plic_driver));

  append_boot_item(bootdata, ZBI_TYPE_KERNEL_DRIVER, KDRV_RISCV_GENERIC_TIMER, &timer_driver,
                   sizeof(timer_driver));

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
