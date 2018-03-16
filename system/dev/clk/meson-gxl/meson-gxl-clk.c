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
#include <soc/aml-meson/gxl-clk.h>

#define GXL_HHI_GCLK_MPEG0 0x50
#define GXL_HHI_GCLK_MPEG1 0x51
#define GXL_HHI_GCLK_MPEG2 0x52
#define GXL_HHI_GCLK_OTHER 0x54

static meson_clk_gate_t gxl_clk_gates[] = {
    // MPEG0 Domain Clocks
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  0},     // CLK_GXL_DDR
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  1},     // CLK_GXL_DOS
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  5},     // CLK_GXL_ISA
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  6},     // CLK_GXL_PL301
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  7},     // CLK_GXL_PERIPHS
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  8},     // CLK_GXL_SPICC
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  9},     // CLK_GXL_I2C
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  10},    // CLK_GXL_SANA
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  11},    // CLK_GXL_SMART_CARD
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  12},    // CLK_GXL_RNG0
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  13},    // CLK_GXL_UART0
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  14},    // CLK_GXL_SDHC
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  15},    // CLK_GXL_STREAM
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  16},    // CLK_GXL_ASYNC_FIFO
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  17},    // CLK_GXL_SDIO
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  18},    // CLK_GXL_ABUF
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  19},    // CLK_GXL_HIU_IFACE
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  22},    // CLK_GXL_BT656
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  23},    // CLK_GXL_ASSIST_MISC
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  24},    // CLK_GXL_EMMC_A
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  25},    // CLK_GXL_EMMC_B
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  26},    // CLK_GXL_EMMC_C
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  27},    // CLK_GXL_DMA
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  28},    // CLK_GXL_ACODEC
    {.reg = GXL_HHI_GCLK_MPEG0, .bit =  30},    // CLK_GXL_SPI

    // MPEG1 Domain Clocks
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  0},     // CLK_GXL_PCLK_TVFE
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  2},     // CLK_GXL_I2S_SPDIF
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  3},     // CLK_GXL_ETH
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  4},     // CLK_GXL_DEMUX
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  6},     // CLK_GXL_AIU_GLUE
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  7},     // CLK_GXL_IEC958
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  8},     // CLK_GXL_I2S_OUT
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  9},     // CLK_GXL_AMCLK
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  10},    // CLK_GXL_AIFIFO2
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  11},    // CLK_GXL_MIXER
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  12},    // CLK_GXL_MIXER_IFACE
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  13},    // CLK_GXL_ADC
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  14},    // CLK_GXL_BLKMV
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  15},    // CLK_GXL_AIU_TOP
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  16},    // CLK_GXL_UART1
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  20},    // CLK_GXL_G2D
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  21},    // CLK_GXL_USB0
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  22},    // CLK_GXL_USB1
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  23},    // CLK_GXL_RESET
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  24},    // CLK_GXL_NAND
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  25},    // CLK_GXL_DOS_PARSER
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  26},    // CLK_GXL_USB_GENERAL
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  28},    // CLK_GXL_VDIN1
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  29},    // CLK_GXL_AHB_ARB0
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  30},    // CLK_GXL_EFUSE
    {.reg = GXL_HHI_GCLK_MPEG1, .bit =  31},    // CLK_GXL_BOOT_ROM

    // MPEG2 Domain Clocks
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  1},     // CLK_GXL_AHB_DATA_BUS
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  2},     // CLK_GXL_AHB_CTRL_BUS
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  3},     // CLK_GXL_HDCP22_PCLK
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  4},     // CLK_GXL_HDMITX_PCLK
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  5},     // CLK_GXL_PDM_PCLK
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  6},     // CLK_GXL_BT656_PCLK
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  8},     // CLK_GXL_USB1_TO_DDR
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  9},     // CLK_GXL_USB0_TO_DDR
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  10},    // CLK_GXL_AIU_PCLK
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  11},    // CLK_GXL_MMC_PCLK
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  12},    // CLK_GXL_DVIN
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  15},    // CLK_GXL_UART2
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  22},    // CLK_GXL_SARADC
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  25},    // CLK_GXL_VPU_INTR
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  26},    // CLK_GXL_SEC_AHB_AHB3_BRIDGE
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  27},    // CLK_GXL_APB3_AO
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  28},    // CLK_GXL_MCLK_TVFE
    {.reg = GXL_HHI_GCLK_MPEG2, .bit =  30},    // CLK_GXL_CLK81_GIC

    // Other Domain Clocks
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  1},     // CLK_GXL_VCLK2_VENCI0
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  2},     // CLK_GXL_VCLK2_VENCI1
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  3},     // CLK_GXL_VCLK2_VENCP0
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  4},     // CLK_GXL_VCLK2_VENCP1
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  5},     // CLK_GXL_VCLK2_VENCT0
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  6},     // CLK_GXL_VCLK2_VENCT1
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  7},     // CLK_GXL_VCLK2_OTHER
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  8},     // CLK_GXL_VCLK2_ENCI
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  9},     // CLK_GXL_VCLK2_ENCP
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  10},    // CLK_GXL_DAC_CLK
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  14},    // CLK_GXL_AOCLK_GATE
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  16},    // CLK_GXL_IEC958_GATE
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  20},    // CLK_GXL_ENC480P
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  21},    // CLK_GXL_RNG1
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  22},    // CLK_GXL_VCLK2_ENCT
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  23},    // CLK_GXL_VCLK2_ENCL
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  24},    // CLK_GXL_VCLK2_VENCLMMC
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  25},    // CLK_GXL_VCLK2_VENCL
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  26},    // CLK_GXL_VCLK2_OTHER1
    {.reg = GXL_HHI_GCLK_OTHER, .bit =  31},    // CLK_GXL_EDP
};

static_assert(CLK_GXL_COUNT == countof(gxl_clk_gates),
              "gxl_clk_gates[] and gxl_clk_gate_idx count mismatch");

static const char meson_gxl_clk_name[] = "meson-gxl-clk";

static zx_status_t meson_gxl_clk_bind(void* ctx, zx_device_t* parent) {
    return meson_clk_init(meson_gxl_clk_name, gxl_clk_gates,
                          countof(gxl_clk_gates), parent);
}

static zx_driver_ops_t meson_gxl_clk_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = meson_gxl_clk_bind,
};

ZIRCON_DRIVER_BEGIN(meson_gxl_clk, meson_gxl_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GXL_CLK),
ZIRCON_DRIVER_END(meson_gxl_clk)
