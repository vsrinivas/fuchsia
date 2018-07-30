// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <hw/reg.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>

#include <soc/aml-a113/a113-gpio.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-meson/axg-clk.h>
#include <dev/pci/amlogic-pcie/atu-cfg.h>

#include "gauss.h"

// Note: These are all constants for the PCIe A controller
//       PCIe B is not currently supported.
static const pbus_mmio_t dw_pcie_mmios[] = {
    {   // elbi
        .base = 0xf9800000,
        .length = 0x400000,   // 4MiB
    },
    {   // phy
        .base = 0xff644000,
        .length = 0x2000,     // 8KiB
    },
    {   // cfg
        .base = 0xff646000,
        .length = 0x2000,     // 8KiB
    },
    {   // reset
        .base = 0xffd01080,
        .length = 0x10,       // 16B
    },
    {   // config
        .base = 0xf9c00000,
        .length = 0x400000,   // 4MiB
    },
    {   // clock/plls
        .base = 0xff63c000,
        .length = PAGE_SIZE,
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
        .gpio = A113_GPIOX(19),     // Reset
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

#define CFG_CPU_ADDR_BASE  (0xf9c00000)
#define CFG_CPU_ADDR_LEN   (0x10000)      // 64KiB of CFG Space
#define IO_CPU_ADDR_BASE   (0xf9d00000)
#define IO_CPU_ADDR_LEN    (0x10000)      // 64KiB of IO Space
#define MEM_CPU_ADDR_BASE  (IO_CPU_ADDR_BASE + IO_CPU_ADDR_LEN)
#define MEM_CPU_ADDR_LEN   (0x300000)     // 3MiB of memory space.

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
        .extra = 0,
        .data = &cfg_entry,
        .len = sizeof(cfg_entry),
    },

    // PCIe IO Space
    {
        .type = IATU_IO_APERTURE_METADATA,  // Private Metadata
        .extra = 0,
        .data = &io_entry,
        .len = sizeof(io_entry),
    },

    // PCIe Memory space
    {
        .type = IATU_MMIO_APERTURE_METADATA,  // Private Metadata
        .extra = 0,
        .data = &mem_entry,
        .len = sizeof(mem_entry),
    },
};

static const pbus_dev_t pcie_dev = {
    .name = "aml-dw-pcie",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_DW_PCIE,
    .mmios = dw_pcie_mmios,
    .mmio_count = countof(dw_pcie_mmios),
    .gpios = dw_pcie_gpios,
    .gpio_count = countof(dw_pcie_gpios),
    .clks = pcie_clk_gates,
    .clk_count = countof(pcie_clk_gates),
    .irqs = dw_pcie_irqs,
    .irq_count = countof(dw_pcie_irqs),
    .metadata = iatu_metadata,
    .metadata_count = countof(iatu_metadata),
};

zx_status_t gauss_pcie_init(gauss_bus_t* bus) {
    zx_status_t st = pbus_device_add(&bus->pbus, &pcie_dev, 0);
    if (st != ZX_OK) {
        zxlogf(ERROR, "gauss_clk_init: pbus_device_add failed, st = %d\n", st);
        return st;
    }

    return ZX_OK;
}