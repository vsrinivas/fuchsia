// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <dev/pci/designware/atu-cfg.h>
#include <hw/reg.h>
#include <soc/aml-a113/a113-gpio.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-meson/axg-clk.h>

#include "gauss.h"

// Disabled until these drivers are converted to use composite device model.
#define ENABLE_PCIE 0

#if ENABLE_PCIE

// Note: These are all constants for the PCIe A controller
//       PCIe B is not currently supported.
static const pbus_mmio_t dw_pcie_mmios[] = {
    {
        // elbi
        .base = 0xf9800000,
        .length = 0x400000,  // 4MiB
    },
    {
        // cfg
        .base = 0xff646000,
        .length = 0x2000,  // 8KiB
    },
    {
        // reset
        .base = 0xffd01080,
        .length = 0x10,  // 16B
    },
    {
        // clock/plls
        .base = 0xff63c000,
        .length = ZX_PAGE_SIZE,
    },
};

static const pbus_irq_t dw_pcie_irqs[] = {
    {
        .irq = DW_PCIE_IRQ0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = DW_PCIE_IRQ1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

static const pbus_gpio_t dw_pcie_gpios[] = {
    {
        .gpio = A113_GPIOX(19),  // Reset
    },
};

static const pbus_clk_t pcie_clk_gates[] = {
    {
        .clk = CLK_AXG_CLK81,
    },
    {
        .clk = CLK_AXG_PCIE_A,
    },
    {
        .clk = CLK_CML0_EN,
    },
};

#define CFG_CPU_ADDR_BASE (0xf9c00000)
#define CFG_CPU_ADDR_LEN (0x10000)  // 64KiB of CFG Space
#define IO_CPU_ADDR_BASE (0xf9d00000)
#define IO_CPU_ADDR_LEN (0x10000)  // 64KiB of IO Space
#define MEM_CPU_ADDR_BASE (IO_CPU_ADDR_BASE + IO_CPU_ADDR_LEN)
#define MEM_CPU_ADDR_LEN (0x300000)  // 3MiB of memory space.

static const iatu_translation_entry_t cfg_entry = {
    .cpu_addr = CFG_CPU_ADDR_BASE,
    .pci_addr = 0,
    .length = CFG_CPU_ADDR_LEN,
};

static const iatu_translation_entry_t io_entry = {
    .cpu_addr = IO_CPU_ADDR_BASE,
    .pci_addr = 0,
    .length = IO_CPU_ADDR_LEN,
};

static const iatu_translation_entry_t mem_entry = {
    .cpu_addr = MEM_CPU_ADDR_BASE,
    .pci_addr = MEM_CPU_ADDR_BASE,
    .length = MEM_CPU_ADDR_LEN,
};

static const pbus_metadata_t iatu_metadata[] = {
    // PCIe Configuration Space
    {
        .type = IATU_CFG_APERTURE_METADATA,  // Private Metadata
        .data_buffer = &cfg_entry,
        .data_size = sizeof(cfg_entry),
    },

    // PCIe IO Space
    {
        .type = IATU_IO_APERTURE_METADATA,  // Private Metadata
        .data_buffer = &io_entry,
        .data_size = sizeof(io_entry),
    },

    // PCIe Memory space
    {
        .type = IATU_MMIO_APERTURE_METADATA,  // Private Metadata
        .data_buffer = &mem_entry,
        .data_size = sizeof(mem_entry),
    },
};

static const pbus_bti_t pci_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static const pbus_dev_t pcie_dev_children[] = {
    {// Resources for child-1
     .bti_list = pci_btis,
     .bti_count = countof(pci_btis)},
};

static const pbus_dev_t pcie_dev = {
    .name = "aml-dw-pcie",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_DW_PCIE,
    .mmio_list = dw_pcie_mmios,
    .mmio_count = countof(dw_pcie_mmios),
    .gpio_list = dw_pcie_gpios,
    .gpio_count = countof(dw_pcie_gpios),
    .clk_list = pcie_clk_gates,
    .clk_count = countof(pcie_clk_gates),
    .irq_list = dw_pcie_irqs,
    .irq_count = countof(dw_pcie_irqs),
    .metadata_list = iatu_metadata,
    .metadata_count = countof(iatu_metadata),
    .bti_list = pci_btis,
    .bti_count = countof(pci_btis),

    // Allow this device to publish the Kernel PCI device on the Platform Bus
    .child_list = pcie_dev_children,
    .child_count = countof(pcie_dev_children),
};
#endif  // ENABLE_PCIE

zx_status_t gauss_pcie_init(gauss_bus_t* bus) {
#if ENABLE_PCIE
  zx_status_t st = pbus_device_add(&bus->pbus, &pcie_dev);
  if (st != ZX_OK) {
    zxlogf(ERROR, "gauss_clk_init: pbus_device_add failed, st = %d\n", st);
    return st;
  }
#endif

  return ZX_OK;
}
