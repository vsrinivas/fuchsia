// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/threads.h>

#include <hw/reg.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include "aml-pcie-clk.h"
#include "aml-pcie-regs.h"

// Extract a uint64_t into the High and Low 32 bits.
#define MASK32 (0xffffffff)
#define HI32(v) (((v) >> 32) & MASK32)
#define LO32(v) ((v) & MASK32)

// Assert this GPIO to reset the PCIe phy
#define GPIO_PRT_RESET   0

typedef enum dw_pcie_addr_window {
    ELBI_WINDOW = 0,
    PHY_WINDOW,
    CFG_WINDOW,
    RESET_WINDOW,
    CONFIG_WINDOW,

    // PLL Window is common for all devices, this should be factored into its
    // own driver.
    PLL_WINDOW,

    ADDR_WINDOW_COUNT,  // always last
} dw_pcie_addr_window_t;

typedef enum dw_pcie_clks {
    CLK81 = 0,
    CLK_PCIEA,
    CLK_PORT,
} dw_pcie_clks_t;

typedef struct dw_pcie {
    zx_device_t* zxdev;

    io_buffer_t buffers[ADDR_WINDOW_COUNT];

    // Protocols structs from the parent driver that we need to function.
    platform_device_protocol_t pdev;
    gpio_protocol_t gpio;
    clk_protocol_t clk;
} dw_pcie_t;

static void dw_pcie_release(void* ctx) {
    dw_pcie_t* pcie = (dw_pcie_t*)ctx;

    for (dw_pcie_addr_window_t wnd = 0; wnd < ADDR_WINDOW_COUNT; ++wnd) {
        io_buffer_release(&pcie->buffers[wnd]);
    }

    free(pcie);
}

static zx_protocol_device_t dw_pcie_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = dw_pcie_release,
};

static inline void clear_reset(dw_pcie_t* pcie, uint32_t bits) {
    volatile uint32_t* reg = io_buffer_virt(&pcie->buffers[RESET_WINDOW]);
    uint32_t val = readl(reg);
    val |= bits;
    writel(val, reg);
}

static inline void assert_reset(dw_pcie_t* pcie, uint32_t bits) {
    volatile uint32_t* reg = io_buffer_virt(&pcie->buffers[RESET_WINDOW]);
    uint32_t val = readl(reg);
    val &= ~bits;
    writel(val, reg);
}
/*
 * Program a region into the outbound ATU
 * The ATU supports 16 regions that can be programmed independently.
 *   pcie,              PCIe Device Struct
 *   index,             Which iATU region are we programming?
 *   type,              Type of PCIe txn being generated on the PCIe bus
 *   cpu_addr,          Physical source address to translate in the CPU's address space
 *   pci_addr,          Destination Address in the PCIe address space
 *   size               Size of the aperature that we're translating.
 */
static zx_status_t dw_program_outbound_atu(dw_pcie_t* pcie,
                                           const uint32_t index,
                                           const uint32_t type,
                                           const zx_paddr_t cpu_addr,
                                           const uintptr_t pci_addr,
                                           const size_t size) {
    // The ATU supports a limited number of regions.
    ZX_DEBUG_ASSERT(index < ATU_REGION_COUNT);

    // Each ATU region has its own bank of registers at this offset from the
    // DBI base
    const size_t bank_offset = (0x3 << 20) | (index << 9);
    volatile uint8_t* atu_base =
        (volatile uint8_t*)(io_buffer_virt(&pcie->buffers[ELBI_WINDOW]) + bank_offset);

    volatile atu_ctrl_regs_t* regs = (volatile atu_ctrl_regs_t*)(atu_base);

    // Memory transactions that are in the following range will get translated
    // to PCI bus transactions:
    //
    // [cpu_addr, cpu_addr + size - 1]
    regs->unroll_lower_base = LO32(cpu_addr);
    regs->unroll_upper_base = HI32(cpu_addr);

    regs->unroll_limit = LO32(cpu_addr + size - 1);

    // Target of the transactions above.
    regs->unroll_lower_target = LO32(pci_addr);
    regs->unroll_upper_target = HI32(pci_addr);

    // Region Ctrl 1 contains a number of fields. The Low 5 bits of the field
    // indicate the type of transaction to dispatch onto the PCIe bus.
    regs->region_ctrl1 = type;

    // Each region can individually be marked as Enabled or Disabled.
    regs->region_ctrl2 |= ATU_REGION_CTRL2_ENABLE;
    regs->region_ctrl2 |= ATU_CFG_SHIFT_MODE;

    // Wait for the enable to take effect.
    for (unsigned int i = 0; i < ATU_PROGRAM_RETRIES; ++i) {
        if (regs->region_ctrl2 & ATU_REGION_CTRL2_ENABLE) {
            return ZX_OK;
        }

        // Wait a little bit before trying again.
        zx_nanosleep(zx_deadline_after(ZX_USEC(ATU_WAIT_ENABLE_TIMEOUT_US)));
    }

    zxlogf(ERROR, "dw_pcie: timed out while awaiting atu enable\n");

    return ZX_ERR_TIMED_OUT;
}

static void configure_root_bridge(volatile uint8_t* rb_ecam) {
    // PCIe Type 1 header Bus Register (offset 0x18 into the Ecam)
    volatile uint8_t* addr = rb_ecam + PCIE_HEADER_BUS_REG_OFF;
    uint32_t bus_reg = readl(addr);

    pci_bus_reg_t* reg = (pci_bus_reg_t*)(&bus_reg);

    // The Upstream Bus for the root bridge is Bus 0
    reg->primary_bus = 0x0;

    // The Downstream bus for the root bridge is Bus 1
    reg->secondary_bus = 0x1;

    // This bridge will also claim all transactions for any other bus IDs on
    // this bus.
    reg->subordinate_bus = 0x1;

    writel(bus_reg, addr);

    // Zero out the BARs for the Root bridge because the DW root bridge doesn't
    // need them.
    writel(0, rb_ecam + PCI_TYPE1_BAR0);
    writel(0, rb_ecam + PCI_TYPE1_BAR1);
}

static void aml_pcie_gpio_reset(dw_pcie_t* pcie) {
    gpio_write(&pcie->gpio, GPIO_PRT_RESET, 0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    gpio_write(&pcie->gpio, GPIO_PRT_RESET, 1);
}


static void aml_pcie_init(dw_pcie_t* pcie) {
    uint32_t val;

    volatile uint8_t* elb = (volatile uint8_t*)io_buffer_virt(&pcie->buffers[ELBI_WINDOW]);
    volatile uint8_t* cfg = (volatile uint8_t*)io_buffer_virt(&pcie->buffers[CFG_WINDOW]);

    val = readl(cfg);
    val |= APP_LTSSM_ENABLE;
    writel(val, cfg);

    val = readl(elb + PORT_LINK_CTRL_OFF);
    val |= PLC_FAST_LINK_MODE;
    writel(val, elb + PORT_LINK_CTRL_OFF);

    val = readl(elb + PORT_LINK_CTRL_OFF);
    val &= ~PLC_LINK_CAPABLE_MASK;
    writel(val, elb + PORT_LINK_CTRL_OFF);

    val = readl(elb + PORT_LINK_CTRL_OFF);
    val |= PLC_LINK_CAPABLE_X1;
    writel(val, elb + PORT_LINK_CTRL_OFF);

    val = readl(elb + GEN2_CTRL_OFF);
    val &= ~G2_CTRL_NUM_OF_LANES_MASK;
    writel(val, elb + GEN2_CTRL_OFF);

    val = readl(elb + GEN2_CTRL_OFF);
    val |= G2_CTRL_NO_OF_LANES(1);
    writel(val, elb + GEN2_CTRL_OFF);

    val = readl(elb + GEN2_CTRL_OFF);
    val |= G2_CTRL_DIRECT_SPEED_CHANGE;
    writel(val, elb + GEN2_CTRL_OFF);
}

static void pcie_rmw_ctrl_sts(volatile uint8_t* ecam, const uint32_t size, 
                              const uint32_t shift, const uint32_t mask) {
    volatile uint8_t* reg = (ecam + PCIE_CTRL_STS_OFF);
    uint32_t val = readl(reg);
    uint32_t regval;

    switch (size) {
        case 128:
            regval = 0;
            break;
        case 256:
            regval = 1;
            break;
        case 512:
            regval = 2;
            break;
        case 1024:
            regval = 3;
            break;
        case 2048:
            regval = 4;
            break;
        case 4096:
            regval = 5;
            break;
        default:
            regval = 1;
    }

    val &= ~(mask << shift);
    writel(val, reg);

    val = readl(reg);
    val |= (regval << shift);
    writel(val, reg);
}

static void pcie_set_max_payload(volatile uint8_t* ecam, const uint32_t size) {
    const uint32_t max_payload_shift = 5;
    const uint32_t max_payload_mask = 0x7;
    pcie_rmw_ctrl_sts(ecam, size, max_payload_shift, max_payload_mask);
}

static void pcie_set_max_read_reqeust_size(volatile uint8_t* ecam,
                                           const uint32_t size) {
    const uint32_t max_rr_size_shift = 5;
    const uint32_t max_rr_size_mask = 0x7;
    pcie_rmw_ctrl_sts(ecam, size, max_rr_size_shift, max_rr_size_mask);
}

static void aml_enable_memory_space(volatile uint8_t* ecam) {
    // Cause the root port to handle transactions.
    volatile uint8_t* reg = (volatile uint8_t*)(ecam + PCIE_TYPE1_STS_CMD_OFF);
    uint32_t val = readl(reg);

    val |= (PCIE_TYPE1_STS_CMD_IO_ENABLE |
            PCIE_TYPE1_STS_CMD_MEM_SPACE_ENABLE |
            PCIE_TYPE1_STS_CMD_BUS_MASTER_ENABLE);
    
    writel(val, reg);
}

static void aml_link_speed_change(volatile uint8_t* elbi) {
    volatile uint8_t* reg = (volatile uint8_t*)(elbi + GEN2_CTRL_OFF);

    uint32_t val = readl(reg);
    val |= G2_CTRL_DIRECT_SPEED_CHANGE;
    writel(val, reg);
}

bool is_link_up(volatile uint8_t* cfg) {
    volatile uint8_t* reg = cfg + PCIE_CFG_STATUS12;

    uint32_t val = readl(reg);

    return (val & PCIE_CFG12_SMLH_UP) &&
           (val & PCIE_CFG12_RDLH_UP) &&
           ((val & PCIE_CFG12_LTSSM_MASK) == PCIE_CFG12_LTSSM_UP);
}

static zx_status_t await_link_up(volatile uint8_t* cfg) {
    for (unsigned int i = 0; i < 500000; i++) {
        if (is_link_up(cfg)) {
            zxlogf(SPEW, "aml dw pcie link up ok");
            return ZX_OK;
        }

        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    }

    return ZX_ERR_TIMED_OUT;
}

static zx_status_t aml_pcie_establish_link(dw_pcie_t* pcie) {
    volatile uint8_t* elbi = io_buffer_virt(&pcie->buffers[ELBI_WINDOW]);
    volatile uint8_t* cfg = io_buffer_virt(&pcie->buffers[CFG_WINDOW]);

    aml_pcie_gpio_reset(pcie);

    aml_pcie_init(pcie);

    pcie_set_max_payload(elbi, 256);

    pcie_set_max_read_reqeust_size(elbi, 256);

    aml_enable_memory_space(elbi);

    aml_link_speed_change(elbi);

    zx_status_t st = await_link_up(cfg);

    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed awaiting link up\n");
        return st;
    }

    configure_root_bridge(elbi);

    return ZX_OK;
}

static zx_status_t init_kernel_pci_driver(dw_pcie_t* pcie) {
    zx_status_t st;

    const size_t pci_sz = io_buffer_size(&pcie->buffers[CONFIG_WINDOW], 0);
    const zx_paddr_t pci_base = 0xf9c00000;
    const size_t ecam_sz = 1 * 1024 * 1024;
    const zx_paddr_t mmio_base = pci_base + ecam_sz;
    const size_t mmio_sz = pci_sz - ecam_sz;

    // Carve out one ECAM for our downstream device.
    if (pci_sz < ecam_sz) {
        zxlogf(ERROR, "dw_pcie: Could not allocate memory aperture for pcie\n");
        return ZX_ERR_NO_RESOURCES;
    }

    st = dw_program_outbound_atu(pcie, 0, PCIE_TLP_TYPE_CFG0,
                                 (zx_paddr_t)pci_base, 0,
                                 ATU_MIN_REGION_SIZE);
    if (st != ZX_OK) {
        zxlogf(ERROR, "dw_pcie: failed to program outbound atu, st = %d\n", st);
        return st;
    }

    // The rest of the space belongs to the PCIe bars and the bus driver is free
    // to allocate it however it pleases.
    st = dw_program_outbound_atu(pcie, 1 << 1, PCIE_TLP_TYPE_MEM_RW,
                                 mmio_base, mmio_base, mmio_sz);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to program outbound atu for ECAM "
               "st = %d\n", st);
        return st;
    }

    // Fire up the kernel PCI driver!
    zx_pci_init_arg_t* arg;
    const size_t arg_size = sizeof(*arg) + sizeof(arg->addr_windows[0]);
    arg = calloc(1, arg_size);
    if (!arg) {
        zxlogf(ERROR, "aml_pcie: failed to allocate pci init arg\n");
        return ZX_ERR_NO_MEMORY;
    }

    st = zx_pci_add_subtract_io_range(get_root_resource(), true, mmio_base,
                                      mmio_sz, true);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to add pcie mmio range, st = %d\n", st);
        goto free_and_fail;
    }

    arg->num_irqs = 0;
    arg->addr_window_count = 1;
    arg->addr_windows[0].is_mmio = true;
    arg->addr_windows[0].has_ecam = true;
    arg->addr_windows[0].base = pci_base;
    arg->addr_windows[0].size = ecam_sz;
    arg->addr_windows[0].bus_start = 0;
    arg->addr_windows[0].bus_end = 0xff;

    st = zx_pci_init(get_root_resource(), arg, arg_size);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to init pci bus driver, st = %d\n", st);
        goto free_and_fail;
    }

free_and_fail:
    free(arg);
    return st;
}

static int dw_pcie_init_thrd(void* arg) {
    zx_status_t st;
    dw_pcie_t* pcie = (dw_pcie_t*)arg;

    assert_reset(pcie, RST_PCIE_A | RST_PCIE_B | RST_PCIE_APB | RST_PCIE_PHY);

    // Enable the PLL and configure it to run at 100MHz
    pcie_pll_set_rate((zx_vaddr_t)io_buffer_virt(&pcie->buffers[PLL_WINDOW]));

    clear_reset(pcie, RST_PCIE_APB | RST_PCIE_PHY);

    st = clk_enable(&pcie->clk, CLK81);
    if (st != ZX_OK) {
        zxlogf(ERROR, "dw_pcie_init_thrd: failed to start clk81, st = %d", st);
        goto fail;
    }

    st = clk_enable(&pcie->clk, CLK_PCIEA);
    if (st != ZX_OK) {
        zxlogf(ERROR, "dw_pcie_init_thrd: failed to start clk pciea, st = %d",
               st);
        goto fail;
    }

    clear_reset(pcie, RST_PCIE_A);

    st = clk_enable(&pcie->clk, CLK_PORT);
    if (st != ZX_OK) {
        zxlogf(ERROR, "dw_pcie_init_thrd: failed to enable port clock, st = %d",
               st);
        goto fail;
    }

    st = aml_pcie_establish_link(pcie);
    if (st != ZX_OK) {
        zxlogf(ERROR, "dw_pcie_init_thrd: failed waiting for link up, st = %d",
               st);
        goto fail;
    }

    init_kernel_pci_driver(pcie);

    // Device added successfully, make it visible.
    device_make_visible(pcie->zxdev);

    return 0;

fail:
    st = device_remove(pcie->zxdev);

    if (st != ZX_OK) {
        zxlogf(ERROR, "dw_pcie_init_thrd: failed to cleanup on failure, st"
                      " = %d", st);
    }

    return -1;
}

static zx_status_t aml_pcie_bind(void* ctx, zx_device_t* parent) {
    zx_status_t st;

    dw_pcie_t* pcie = malloc(sizeof(*pcie));
    if (!pcie) {
        zxlogf(ERROR, "aml_pcie_bind: failed to allocate pcie struct");
        return ZX_ERR_NO_MEMORY;
    }

    st = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pcie->pdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie_bind: failed to get platform device protocol "
                      "st = %d", st);
        goto fail;
    }

    st = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &pcie->gpio);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie_bind: failed to get platform gpio protocol "
                      "st = %d", st);
        goto fail;
    }

    st = device_get_protocol(parent, ZX_PROTOCOL_CLK, &pcie->clk);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie_bind: failed to get platform clk protocol "
                      "st = %d", st);
        goto fail;
    }

    // Configure the reset gpio
    gpio_config(&pcie->gpio, GPIO_PRT_RESET, GPIO_DIR_OUT);

    // Map all the MMIO windows that we're interested in.
    for (dw_pcie_addr_window_t wnd = 0; wnd < ADDR_WINDOW_COUNT; ++wnd) {
        st = pdev_map_mmio_buffer(&pcie->pdev,
                                  wnd,  /* Index of addr window */
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &pcie->buffers[wnd]);

        if (st != ZX_OK) {
            zxlogf(ERROR, "aml_pcie_bind: failed to map mmio window #%u, "
                          "rc = %d", wnd, st);
            goto fail;
        }
    }

    zx_device_prop_t props[] = {
        { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC },
        { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC },
        { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_KPCI },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-dw-pcie",
        .ctx = pcie,
        .ops = &dw_pcie_device_proto,
        .flags = DEVICE_ADD_INVISIBLE,  // Made visible by init thread.
        .props = props,
        .prop_count = countof(props),
    };

    args.proto_id = ZX_PROTOCOL_PLATFORM_DEV;
    args.proto_ops = &pcie->pdev;

    st = device_add(parent, &args, &pcie->zxdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie_bind: failed to add device, st = %d", st);
        goto fail;
    }

    thrd_t init_thrd;
    st = thrd_status_to_zx_status(
        thrd_create_with_name(&init_thrd, dw_pcie_init_thrd, pcie,
                              "aml-dw-pcie-init")
    );
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie_bind: failed to start init thread, st = %d",
                      st);
        goto fail;
    }

    thrd_detach(init_thrd);

    return ZX_OK;

fail:
    // Clean up any resources that we've allocated.
    dw_pcie_release(pcie);

    // Sanity check: make sure we never return ZX_OK from the fail path since
    // this will tell devmgr that the bind was successful.
    ZX_DEBUG_ASSERT(st != ZX_OK);
    return st;
}

static zx_driver_ops_t aml_pcie_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_pcie_bind,
};

// Bind to ANY Amlogic SoC with a DWC PCIe controller.
ZIRCON_DRIVER_BEGIN(aml_pcie, aml_pcie_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DW_PCIE),
ZIRCON_DRIVER_END(aml_pcie)