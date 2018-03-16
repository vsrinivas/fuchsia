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

#include <dev/clk/meson-lib/meson.h>
#include <soc/aml-meson/axg-clk.h>

#define AXG_HHI_PCIE_PLL_CNTL6   0x3C
#define AXG_HHI_GCLK_MPEG0       0x50
#define AXG_HHI_GCLK_MPEG1       0x51
#define AXG_HHI_GCLK_MPEG2       0x52
#define AXG_HHI_GCLK_AO          0x55
#define AXG_HHI_MPEG_CLK_CNTL    0x5D

static meson_clk_gate_t axg_clk_gates[] = {
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

    // Etc...
    {.reg = AXG_HHI_MPEG_CLK_CNTL, .bit = 7},     // CLK_AXG_CLK81
    {.reg = AXG_HHI_PCIE_PLL_CNTL6, .bit = 4},    // CLK_CML0_EN
};

static_assert(CLK_AXG_COUNT == countof(axg_clk_gates),
              "axg_clk_gates[] and axg_clk_gate_idx count mismatch");

static const char meson_axg_clk_name[] = "meson-axg-clk";

static zx_status_t meson_axg_clk_bind(void* ctx, zx_device_t* parent) {
    return meson_clk_init(meson_axg_clk_name, axg_clk_gates,
                          countof(axg_clk_gates), parent);
}

static zx_driver_ops_t meson_axg_clk_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = meson_axg_clk_bind,
};

ZIRCON_DRIVER_BEGIN(meson_axg_clk, meson_axg_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_AXG_CLK),
ZIRCON_DRIVER_END(meson_axg_clk)
