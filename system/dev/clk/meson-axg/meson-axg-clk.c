// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/reg.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <soc/aml-a113/a113-hw.h>

typedef struct axg_clk_gate {
    uint32_t reg;
    uint32_t bit;
} axg_clk_gate_t;

static const axg_clk_gate_t axg_clk_gates[] = {
    // MPEG0 Clock Gates
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 0},     // CLK_AXG_DDR
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 2},     // CLK_AXG_AUDIO_LOCKER
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 3},     // CLK_AXG_MIPI_DSI_HOST
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 5},     // CLK_AXG_ISA
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 6},     // CLK_AXG_PL301
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 7},     // CLK_AXG_PERIPHS
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 8},     // CLK_AXG_SPICC_0
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 9},     // CLK_AXG_I2C
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 12},    // CLK_AXG_RNG0
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 13},    // CLK_AXG_UART0
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 14},    // CLK_AXG_MIPI_DSI_PHY
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 15},    // CLK_AXG_SPICC_1
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 16},    // CLK_AXG_PCIE_A
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 17},    // CLK_AXG_PCIE_B
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 19},    // CLK_AXG_HIU_REG
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 23},    // CLK_AXG_ASSIST_MISC
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 25},    // CLK_AXG_EMMC_B
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 26},    // CLK_AXG_EMMC_C
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 27},    // CLK_AXG_DMA
    {.reg = AXG_HHI_GCLK_MPEG0, .bit = 30},    // CLK_AXG_SPI

    // MPEG1 Clock Gates
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 0},     // CLK_AXG_AUDIO
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 3},     // CLK_AXG_ETH_CORE
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 16},    // CLK_AXG_UART1
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 20},    // CLK_AXG_G2D
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 21},    // CLK_AXG_USB0
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 22},    // CLK_AXG_USB1
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 23},    // CLK_AXG_RESET
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 26},    // CLK_AXG_USB_GENERAL
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 29},    // CLK_AXG_AHB_ARB0
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 30},    // CLK_AXG_EFUSE
    {.reg = AXG_HHI_GCLK_MPEG1, .bit = 31},    // CLK_AXG_BOOT_ROM

    // MPEG2 Clock Gates
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 1},     // CLK_AXG_AHB_DATA_BUS
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 2},     // CLK_AXG_AHB_CTRL_BUS
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 8},     // CLK_AXG_USB1_TO_DDR
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 9},     // CLK_AXG_USB0_TO_DDR
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 11},    // CLK_AXG_MMC_PCLK
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 25},    // CLK_AXG_VPU_INTR
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 26},    // CLK_AXG_SEC_AHB_AHB3_BRIDGE
    {.reg = AXG_HHI_GCLK_MPEG2, .bit = 30},    // CLK_AXG_GIC
    
    // AO Domain Clock Gates
    {.reg = AXG_HHI_GCLK_AO, .bit = 0},       // CLK_AXG_AO_MEDIA_CPU
    {.reg = AXG_HHI_GCLK_AO, .bit = 1},       // CLK_AXG_AO_AHB_SRAM
    {.reg = AXG_HHI_GCLK_AO, .bit = 2},       // CLK_AXG_AO_AHB_BUS
    {.reg = AXG_HHI_GCLK_AO, .bit = 3},       // CLK_AXG_AO_IFACE
    {.reg = AXG_HHI_GCLK_AO, .bit = 4},       // CLK_AXG_AO_I2C
};

static_assert(CLK_AXG_COUNT == countof(axg_clk_gates),
              "axg_clk_gates[] and axg_clk_gate_idx count mismatch");

typedef struct meson_axg_clk {
    platform_device_protocol_t pdev;
    clk_protocol_t clk;
    zx_device_t* zxdev;
    pdev_vmo_buffer_t mmio;

    // Serialize access to clocks.
    mtx_t lock;
} meson_axg_clk_t;


static zx_status_t meson_axg_clk_toggle(void* ctx, const uint32_t clk,
                                        const bool enable) {
    const axg_clk_gate_idx_t idx = clk;
    meson_axg_clk_t* const meson_clk = ctx;

    if (idx >= CLK_AXG_COUNT) return ZX_ERR_INVALID_ARGS;

    const axg_clk_gate_t* const gate = &axg_clk_gates[idx];

    volatile uint32_t* regs = (volatile uint32_t*)meson_clk->mmio.vaddr;

    mtx_lock(&meson_clk->lock);

    uint32_t val = readl(regs + gate->reg);

    if (enable) {
        val |= (1 << gate->bit);
    } else {
        val &= ~(1 << gate->bit);
    }

    writel(val, regs + gate->reg);

    mtx_unlock(&meson_clk->lock);

    return ZX_OK;
}

static zx_status_t meson_axg_clk_enable(void* ctx, uint32_t clk) {
    return meson_axg_clk_toggle(ctx, clk, true);
}

static zx_status_t meson_axg_clk_disable(void* ctx, uint32_t clk) {
    return meson_axg_clk_toggle(ctx, clk, false);
}

clk_protocol_ops_t clk_ops = {
    .enable = meson_axg_clk_enable,
    .disable = meson_axg_clk_disable,
};

static void meson_axg_release(void* ctx) {
    meson_axg_clk_t* axg_clk = ctx;
    pdev_vmo_buffer_release(&axg_clk->mmio);
    mtx_destroy(&axg_clk->lock);
    free(axg_clk);
}

static zx_protocol_device_t meson_axg_clk_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = meson_axg_release,
};

static zx_status_t meson_axg_clk_bind(void* ctx, zx_device_t* parent) {
    zx_status_t st = ZX_ERR_INTERNAL;

    meson_axg_clk_t* meson_clk = calloc(1, sizeof(*meson_clk));
    if (!meson_clk) {
        zxlogf(ERROR, "meson_axg_clk_bind: failed to allocate meson_clk\n");
        return ZX_ERR_NO_MEMORY;
    }

    int ret = mtx_init(&meson_clk->lock, mtx_plain);
    if (ret != thrd_success) {
        zxlogf(ERROR, "meson_axg_clk_bind: failed to initialize mutex\n");
        st = thrd_status_to_zx_status(ret);
        goto fail;
    }

    st = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &meson_clk->pdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_axg_clk_bind: Failed to get ZX_PROTOCOL_PLATFORM_DEV\n");
        goto fail;
    }

    platform_bus_protocol_t pbus;
    st = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_axg_clk_bind: Failed to get ZX_PROTOCOL_PLATFORM_BUS\n");
        goto fail;
    }

    // Map the MMIOs for this clock
    st = pdev_map_mmio_buffer(&meson_clk->pdev, 0,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &meson_clk->mmio);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_axg_clk_bind: pdev_map_mmio_buffer failed, st = %d\n", st);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "meson-axg-clk",
        .ctx = meson_clk,
        .ops = &meson_axg_clk_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    st = device_add(parent, &args, &meson_clk->zxdev);
    if (st != ZX_OK) {
        zxlogf(ERROR, "meson_axg_clk_bind: device_add failed, st = %d\n", st);
        goto fail;
    }

    meson_clk->clk.ops = &clk_ops;
    meson_clk->clk.ctx = meson_clk;

    pbus_set_protocol(&pbus, ZX_PROTOCOL_CLK, &meson_clk->clk);

    return ZX_OK;

fail:
    meson_axg_release(meson_clk);

    // Make sure we don't accidentally return ZX_OK if the device has failed
    // to bind for some reason
    ZX_DEBUG_ASSERT(st != ZX_OK);
    return st;
}


static zx_driver_ops_t meson_axg_clk_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = meson_axg_clk_bind,
};

ZIRCON_DRIVER_BEGIN(meson_axg_clk, meson_axg_clk_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_A113),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_CLK),
ZIRCON_DRIVER_END(meson_axg_clk)
