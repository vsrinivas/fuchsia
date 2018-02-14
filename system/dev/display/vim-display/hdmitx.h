// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
                        ((mask & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define READ32_PRESET_REG(a)             readl(display->mmio_preset.vaddr + a)
#define WRITE32_PRESET_REG(a, v)         writel(v, display->mmio_preset.vaddr + a)

#define READ32_HDMITX_REG(a)             readl(display->mmio_hdmitx.vaddr + a)
#define WRITE32_HDMITX_REG(a, v)         writel(v, display->mmio_hdmitx.vaddr + a)

#define READ32_HHI_REG(a)                readl(display->mmio_hiu.vaddr + a)
#define WRITE32_HHI_REG(a, v)            writel(v, display->mmio_hiu.vaddr + a)

#define READ32_VPU_REG(a)                readl(display->mmio_vpu.vaddr + a)
#define WRITE32_VPU_REG(a, v)            writel(v, display->mmio_vpu.vaddr + a)

#define READ32_DMC_REG(a)                readl(display->mmio_dmc.vaddr + a)
#define WRITE32_DMC_REG(a, v)            writel(v, display->mmio_dmc.vaddr + a)

#define READ32_HDMITX_SEC_REG(a)         readl(display->mmio_hdmitx_sec.vaddr + a)
#define WRITE32_HDMITX_SEC_REG(a, v)     writel(v, display->mmio_hdmitx_sec.vaddr + a)


#define SET_BIT32(x, dest, value, count, start) \
            WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                                (((value) << (start)) & DISPLAY_MASK(start, count)))

#define WRITE32_REG(x, a, v)    WRITE32_##x##_REG(a, v)
#define READ32_REG(x, a)        READ32_##x##_REG(a)

#define SEC_OFFSET           (0x1UL << 24)
#define TOP_OFFSET_MASK      (0x0UL << 24)
#define TOP_SEC_OFFSET_MASK  ((TOP_OFFSET_MASK) | (SEC_OFFSET))
#define DWC_OFFSET_MASK      (0x10UL << 24)
#define DWC_SEC_OFFSET_MASK  ((DWC_OFFSET_MASK) | (SEC_OFFSET))

#define DMC_CAV_LUT_DATAL           (0x12 << 2)
#define DMC_CAV_LUT_DATAH           (0x13 << 2)
#define DMC_CAV_LUT_ADDR             (0x14 << 2)

#define DMC_CAV_ADDR_LMASK       0x1fffffff
#define DMC_CAV_WIDTH_LMASK      0x7
#define DMC_CAV_WIDTH_LWID       3
#define DMC_CAV_WIDTH_LBIT       29

#define DMC_CAV_WIDTH_HMASK      0x1ff
#define DMC_CAV_WIDTH_HBIT       0
#define DMC_CAV_HEIGHT_MASK      0x1fff
#define DMC_CAV_HEIGHT_BIT       9

#define DMC_CAV_LUT_ADDR_INDEX_MASK  0x7
#define DMC_CAV_LUT_ADDR_RD_EN       (1 << 8)
#define DMC_CAV_LUT_ADDR_WR_EN       (2 << 8)

/* HHI */
#define HHI_MEM_PD_REG0                                 (0x40 << 2)
#define HHI_VPU_MEM_PD_REG0                             (0x41 << 2)
#define HHI_VPU_MEM_PD_REG1                             (0x42 << 2)
#define HHI_AUD_DAC_CTRL                                (0x44 << 2)
#define HHI_VIID_CLK_DIV                                (0x4a << 2)
#define HHI_GCLK_MPEG0                                  (0x50 << 2)
#define HHI_GCLK_MPEG1                                  (0x51 << 2)
#define HHI_GCLK_MPEG2                                  (0x52 << 2)
#define HHI_GCLK_OTHER                                  (0x54 << 2)
#define HHI_GCLK_AO                                     (0x55 << 2)
#define HHI_SYS_OSCIN_CNTL                              (0x56 << 2)
#define HHI_SYS_CPU_CLK_CNTL1                           (0x57 << 2)
#define HHI_SYS_CPU_RESET_CNTL                          (0x58 << 2)
#define HHI_VID_CLK_DIV                                 (0x59 << 2)
#define HHI_MPEG_CLK_CNTL                               (0x5d << 2)
#define HHI_AUD_CLK_CNTL                                (0x5e << 2)
#define HHI_VID_CLK_CNTL                                (0x5f << 2)
#define HHI_WIFI_CLK_CNTL                               (0x60 << 2)
#define HHI_WIFI_PLL_CNTL                               (0x61 << 2)
#define HHI_WIFI_PLL_CNTL2                              (0x62 << 2)
#define HHI_WIFI_PLL_CNTL3                              (0x63 << 2)
#define HHI_AUD_CLK_CNTL2                               (0x64 << 2)
#define HHI_VID_CLK_CNTL2                               (0x65 << 2)
#define HHI_VID_DIVIDER_CNTL                            (0x66 << 2)
#define HHI_SYS_CPU_CLK_CNTL                            (0x67 << 2)
#define HHI_VID_PLL_CLK_DIV                             (0x68 << 2)
#define HHI_AUD_CLK_CNTL3                               (0x69 << 2)
#define HHI_MALI_CLK_CNTL                               (0x6c << 2)
#define HHI_MIPI_PHY_CLK_CNTL                           (0x6e << 2)
#define HHI_VPU_CLK_CNTL                                (0x6f << 2)
#define HHI_OTHER_PLL_CNTL                              (0x70 << 2)
#define HHI_OTHER_PLL_CNTL2                             (0x71 << 2)
#define HHI_OTHER_PLL_CNTL3                             (0x72 << 2)
#define HHI_HDMI_CLK_CNTL                               (0x73 << 2)
#define HHI_DEMOD_CLK_CNTL                              (0x74 << 2)
#define HHI_SATA_CLK_CNTL                               (0x75 << 2)
#define HHI_ETH_CLK_CNTL                                (0x76 << 2)
#define HHI_CLK_DOUBLE_CNTL                             (0x77 << 2)
#define HHI_VDEC_CLK_CNTL                               (0x78 << 2)
#define HHI_VDEC2_CLK_CNTL                              (0x79 << 2)
#define HHI_VDEC3_CLK_CNTL                              (0x7a << 2)
#define HHI_VDEC4_CLK_CNTL                              (0x7b << 2)
#define HHI_HDCP22_CLK_CNTL                             (0x7c << 2)
#define HHI_VAPBCLK_CNTL                                (0x7d << 2)
#define HHI_VP9DEC_CLK_CNTL                             (0x7e << 2)
#define HHI_HDMI_AFC_CNTL                               (0x7f << 2)
#define HHI_HDMIRX_CLK_CNTL                             (0x80 << 2)
#define HHI_HDMIRX_AUD_CLK_CNTL                         (0x81 << 2)
#define HHI_EDP_APB_CLK_CNTL                            (0x82 << 2)
#define HHI_VPU_CLKB_CNTL                               (0x83 << 2)
#define HHI_VID_PLL_MOD_CNTL0                           (0x84 << 2)
#define HHI_VID_PLL_MOD_LOW_TCNT                        (0x85 << 2)
#define HHI_VID_PLL_MOD_HIGH_TCNT                       (0x86 << 2)
#define HHI_VID_PLL_MOD_NOM_TCNT                        (0x87 << 2)
#define HHI_USB_CLK_CNTL                                (0x88 << 2)
#define HHI_32K_CLK_CNTL                                (0x89 << 2)
#define HHI_GEN_CLK_CNTL                                (0x8a << 2)
#define HHI_GEN_CLK_CNTL2                               (0x8b << 2)
#define HHI_JTAG_CONFIG                                 (0x8e << 2)
#define HHI_VAFE_CLKXTALIN_CNTL                         (0x8f << 2)
#define HHI_VAFE_CLKOSCIN_CNTL                          (0x90 << 2)
#define HHI_VAFE_CLKIN_CNTL                             (0x91 << 2)
#define HHI_TVFE_AUTOMODE_CLK_CNTL                      (0x92 << 2)
#define HHI_VAFE_CLKPI_CNTL                             (0x93 << 2)
#define HHI_VDIN_MEAS_CLK_CNTL                          (0x94 << 2)
#define HHI_PCM_CLK_CNTL                                (0x96 << 2)
#define HHI_NAND_CLK_CNTL                               (0x97 << 2)
#define HHI_ISP_LED_CLK_CNTL                            (0x98 << 2)
#define HHI_SD_EMMC_CLK_CNTL                            (0x99 << 2)
#define HHI_EDP_TX_PHY_CNTL0                            (0x9c << 2)
#define HHI_EDP_TX_PHY_CNTL1                            (0x9d << 2)
#define HHI_MPLL_CNTL                                   (0xa0 << 2)
#define HHI_MPLL_CNTL2                                  (0xa1 << 2)
#define HHI_MPLL_CNTL3                                  (0xa2 << 2)
#define HHI_MPLL_CNTL4                                  (0xa3 << 2)
#define HHI_MPLL_CNTL5                                  (0xa4 << 2)
#define HHI_MPLL_CNTL6                                  (0xa5 << 2)
#define HHI_MPLL_CNTL7                                  (0xa6 << 2)
#define HHI_MPLL_CNTL8                                  (0xa7 << 2)
#define HHI_MPLL_CNTL9                                  (0xa8 << 2)
#define HHI_MPLL_CNTL10                                 (0xa9 << 2)
#define HHI_ADC_PLL_CNTL                                (0xaa << 2)
#define HHI_ADC_PLL_CNTL2                               (0xab << 2)
#define HHI_ADC_PLL_CNTL3                               (0xac << 2)
#define HHI_ADC_PLL_CNTL4                               (0xad << 2)
#define HHI_ADC_PLL_CNTL_I                              (0xae << 2)
#define HHI_AUDCLK_PLL_CNTL                             (0xb0 << 2)
#define HHI_AUDCLK_PLL_CNTL2                            (0xb1 << 2)
#define HHI_AUDCLK_PLL_CNTL3                            (0xb2 << 2)
#define HHI_AUDCLK_PLL_CNTL4                            (0xb3 << 2)
#define HHI_AUDCLK_PLL_CNTL5                            (0xb4 << 2)
#define HHI_AUDCLK_PLL_CNTL6                            (0xb5 << 2)
#define HHI_L2_DDR_CLK_CNTL                             (0xb6 << 2)
#define HHI_MPLL3_CNTL0                                 (0xb8 << 2)
#define HHI_MPLL3_CNTL1                                 (0xb9 << 2)
#define HHI_VDAC_CNTL0                                  (0xbd << 2)
#define HHI_VDAC_CNTL1                                  (0xbe << 2)
#define HHI_SYS_PLL_CNTL                                (0xc0 << 2)
#define HHI_SYS_PLL_CNTL2                               (0xc1 << 2)
#define HHI_SYS_PLL_CNTL3                               (0xc2 << 2)
#define HHI_SYS_PLL_CNTL4                               (0xc3 << 2)
#define HHI_SYS_PLL_CNTL5                               (0xc4 << 2)
#define HHI_DPLL_TOP_I                                  (0xc6 << 2)
#define HHI_DPLL_TOP2_I                                 (0xc7 << 2)
#define HHI_HDMI_PLL_CNTL                               (0xc8 << 2)
#define HHI_HDMI_PLL_CNTL1                              (0xc9 << 2)
#define HHI_HDMI_PLL_CNTL2                              (0xca << 2)
#define HHI_HDMI_PLL_CNTL3                              (0xcb << 2)
#define HHI_HDMI_PLL_CNTL4                              (0xcc << 2)
#define HHI_HDMI_PLL_CNTL5                              (0xcd << 2)
#define HHI_HDMI_PLL_STS                                (0xce << 2)
#define HHI_DSI_LVDS_EDP_CNTL0                          (0xd1 << 2)
#define HHI_DSI_LVDS_EDP_CNTL1                          (0xd2 << 2)
#define HHI_CSI_PHY_CNTL0                               (0xd3 << 2)
#define HHI_CSI_PHY_CNTL1                               (0xd4 << 2)
#define HHI_CSI_PHY_CNTL2                               (0xd5 << 2)
#define HHI_CSI_PHY_CNTL3                               (0xd6 << 2)
#define HHI_CSI_PHY_CNTL4                               (0xd7 << 2)
#define HHI_DIF_CSI_PHY_CNTL0                           (0xd8 << 2)
#define HHI_DIF_CSI_PHY_CNTL1                           (0xd9 << 2)
#define HHI_DIF_CSI_PHY_CNTL2                           (0xda << 2)
#define HHI_DIF_CSI_PHY_CNTL3                           (0xdb << 2)
#define HHI_DIF_CSI_PHY_CNTL4                           (0xdc << 2)
#define HHI_DIF_CSI_PHY_CNTL5                           (0xdd << 2)
#define HHI_LVDS_TX_PHY_CNTL0                           (0xde << 2)
#define HHI_LVDS_TX_PHY_CNTL1                           (0xdf << 2)
#define HHI_VID2_PLL_CNTL                               (0xe0 << 2)
#define HHI_VID2_PLL_CNTL2                              (0xe1 << 2)
#define HHI_VID2_PLL_CNTL3                              (0xe2 << 2)
#define HHI_VID2_PLL_CNTL4                              (0xe3 << 2)
#define HHI_VID2_PLL_CNTL5                              (0xe4 << 2)
#define HHI_VID2_PLL_CNTL_I                             (0xe5 << 2)
#define HHI_HDMI_PHY_CNTL0                              (0xe8 << 2)
#define HHI_HDMI_PHY_CNTL1                              (0xe9 << 2)
#define HHI_HDMI_PHY_CNTL2                              (0xea << 2)
#define HHI_HDMI_PHY_CNTL3                              (0xeb << 2)
#define HHI_VID_LOCK_CLK_CNTL                           (0xf2 << 2)
#define HHI_ATV_DMD_SYS_CLK_CNTL                        (0xf3 << 2)
#define HHI_BT656_CLK_CNTL                              (0xf5 << 2)
#define HHI_SAR_CLK_CNTL                                (0xf6 << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL                         (0xf8 << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL2                        (0xf9 << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL3                        (0xfa << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL4                        (0xfb << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL5                        (0xfc << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL6                        (0xfd << 2)
#define HHI_HDMIRX_AUD_PLL_CNTL_I                       (0xfe << 2)

// P RESET
#define PRESET_REGISTER                                 (0x400)
#define PRESET0_REGISTER                                (0x404)
#define PRESET2_REGISTER                                (0x40C)

// HDMITX ADDRESS and DATA PORTS
#define HDMITX_ADDR_PORT                                (0x00)
#define HDMITX_DATA_PORT                                (0x04)
#define HDMITX_CTRL_PORT                                (0x08)


// HDMI TOP
#define HDMITX_TOP_SW_RESET                             (TOP_OFFSET_MASK + 0x000)
#define HDMITX_TOP_CLK_CNTL                             (TOP_OFFSET_MASK + 0x001)
#define HDMITX_TOP_HPD_FILTER                           (TOP_OFFSET_MASK + 0x002)
#define HDMITX_TOP_INTR_MASKN                           (TOP_OFFSET_MASK + 0x003)
#define HDMITX_TOP_INTR_STAT                            (TOP_OFFSET_MASK + 0x004)
#define HDMITX_TOP_INTR_STAT_CLR                        (TOP_OFFSET_MASK + 0x005)
#define HDMITX_TOP_BIST_CNTL                            (TOP_OFFSET_MASK + 0x006)
#define HDMITX_TOP_SHIFT_PTTN_012                       (TOP_OFFSET_MASK + 0x007)
#define HDMITX_TOP_SHIFT_PTTN_345                       (TOP_OFFSET_MASK + 0x008)
#define HDMITX_TOP_SHIFT_PTTN_67                        (TOP_OFFSET_MASK + 0x009)
#define HDMITX_TOP_TMDS_CLK_PTTN_01                     (TOP_OFFSET_MASK + 0x00A)
#define HDMITX_TOP_TMDS_CLK_PTTN_23                     (TOP_OFFSET_MASK + 0x00B)
#define HDMITX_TOP_TMDS_CLK_PTTN_CNTL                   (TOP_OFFSET_MASK + 0x00C)
#define HDMITX_TOP_REVOCMEM_STAT                        (TOP_OFFSET_MASK + 0x00D)
#define HDMITX_TOP_STAT0                                (TOP_OFFSET_MASK + 0x00E)
#define HDMITX_TOP_SKP_CNTL_STAT                        (TOP_SEC_OFFSET_MASK + 0x010)
#define HDMITX_TOP_NONCE_0                              (TOP_SEC_OFFSET_MASK + 0x011)
#define HDMITX_TOP_NONCE_1                              (TOP_SEC_OFFSET_MASK + 0x012)
#define HDMITX_TOP_NONCE_2                              (TOP_SEC_OFFSET_MASK + 0x013)
#define HDMITX_TOP_NONCE_3                              (TOP_SEC_OFFSET_MASK + 0x014)
#define HDMITX_TOP_PKF_0                                (TOP_SEC_OFFSET_MASK + 0x015)
#define HDMITX_TOP_PKF_1                                (TOP_SEC_OFFSET_MASK + 0x016)
#define HDMITX_TOP_PKF_2                                (TOP_SEC_OFFSET_MASK + 0x017)
#define HDMITX_TOP_PKF_3                                (TOP_SEC_OFFSET_MASK + 0x018)
#define HDMITX_TOP_DUK_0                                (TOP_SEC_OFFSET_MASK + 0x019)
#define HDMITX_TOP_DUK_1                                (TOP_SEC_OFFSET_MASK + 0x01A)
#define HDMITX_TOP_DUK_2                                (TOP_SEC_OFFSET_MASK + 0x01B)
#define HDMITX_TOP_DUK_3                                (TOP_SEC_OFFSET_MASK + 0x01C)
#define HDMITX_TOP_INFILTER                             (TOP_OFFSET_MASK + 0x01D)
#define HDMITX_TOP_NSEC_SCRATCH                         (TOP_OFFSET_MASK + 0x01E)
#define HDMITX_TOP_SEC_SCRATCH                          (TOP_SEC_OFFSET_MASK + 0x01F)
#define HDMITX_TOP_DONT_TOUCH0                          (TOP_OFFSET_MASK + 0x0FE)
#define HDMITX_TOP_DONT_TOUCH1                          (TOP_OFFSET_MASK + 0x0FF)


// HDMI DWC
#define HDMITX_DWC_DESIGN_ID                            (DWC_OFFSET_MASK + 0x0000)
#define HDMITX_DWC_REVISION_ID                          (DWC_OFFSET_MASK + 0x0001)
#define HDMITX_DWC_PRODUCT_ID0                          (DWC_OFFSET_MASK + 0x0002)
#define HDMITX_DWC_PRODUCT_ID1                          (DWC_OFFSET_MASK + 0x0003)
#define HDMITX_DWC_CONFIG0_ID                           (DWC_OFFSET_MASK + 0x0004)
#define HDMITX_DWC_CONFIG1_ID                           (DWC_OFFSET_MASK + 0x0005)
#define HDMITX_DWC_CONFIG2_ID                           (DWC_OFFSET_MASK + 0x0006)
#define HDMITX_DWC_CONFIG3_ID                           (DWC_OFFSET_MASK + 0x0007)
#define HDMITX_DWC_IH_FC_STAT0                          (DWC_OFFSET_MASK + 0x0100)
#define HDMITX_DWC_IH_FC_STAT1                          (DWC_OFFSET_MASK + 0x0101)
#define HDMITX_DWC_IH_FC_STAT2                          (DWC_OFFSET_MASK + 0x0102)
#define HDMITX_DWC_IH_AS_STAT0                          (DWC_OFFSET_MASK + 0x0103)
#define HDMITX_DWC_IH_PHY_STAT0                         (DWC_OFFSET_MASK + 0x0104)
#define HDMITX_DWC_IH_I2CM_STAT0                        (DWC_OFFSET_MASK + 0x0105)
#define HDMITX_DWC_IH_CEC_STAT0                         (DWC_OFFSET_MASK + 0x0106)
#define HDMITX_DWC_IH_VP_STAT0                          (DWC_OFFSET_MASK + 0x0107)
#define HDMITX_DWC_IH_I2CMPHY_STAT0                     (DWC_OFFSET_MASK + 0x0108)
#define HDMITX_DWC_IH_DECODE                            (DWC_OFFSET_MASK + 0x0170)
#define HDMITX_DWC_IH_MUTE_FC_STAT0                     (DWC_OFFSET_MASK + 0x0180)
#define HDMITX_DWC_IH_MUTE_FC_STAT1                     (DWC_OFFSET_MASK + 0x0181)
#define HDMITX_DWC_IH_MUTE_FC_STAT2                     (DWC_OFFSET_MASK + 0x0182)
#define HDMITX_DWC_IH_MUTE_AS_STAT0                     (DWC_OFFSET_MASK + 0x0183)
#define HDMITX_DWC_IH_MUTE_PHY_STAT0                    (DWC_OFFSET_MASK + 0x0184)
#define HDMITX_DWC_IH_MUTE_I2CM_STAT0                   (DWC_OFFSET_MASK + 0x0185)
#define HDMITX_DWC_IH_MUTE_CEC_STAT0                    (DWC_OFFSET_MASK + 0x0186)
#define HDMITX_DWC_IH_MUTE_VP_STAT0                     (DWC_OFFSET_MASK + 0x0187)
#define HDMITX_DWC_IH_MUTE_I2CMPHY_STAT0                (DWC_OFFSET_MASK + 0x0188)
#define HDMITX_DWC_IH_MUTE                              (DWC_OFFSET_MASK + 0x01FF)

#define HDMITX_DWC_TX_INVID0                            (DWC_OFFSET_MASK + 0x0200)
 #define TX_INVID0_DE_GEN_ENB                       (0x01 << 7)
 #define TX_INVID0_VM_RGB444_8B                     (0x01 << 0)
 #define TX_INVID0_VM_RGB444_10B                    (0x03 << 0)
 #define TX_INVID0_VM_RGB444_12B                    (0x05 << 0)
 #define TX_INVID0_VM_RGB444_16B                    (0x07 << 0)
 #define TX_INVID0_VM_YCBCR444_8B                   (0x09 << 0)
 #define TX_INVID0_VM_YCBCR444_10B                  (0x0B << 0)
 #define TX_INVID0_VM_YCBCR444_12B                  (0x0D << 0)
 #define TX_INVID0_VM_YCBCR444_16B                  (0x0F << 0)

#define HDMITX_DWC_TX_INSTUFFING                        (DWC_OFFSET_MASK + 0x0201)
#define HDMITX_DWC_TX_GYDATA0                           (DWC_OFFSET_MASK + 0x0202)
#define HDMITX_DWC_TX_GYDATA1                           (DWC_OFFSET_MASK + 0x0203)
#define HDMITX_DWC_TX_RCRDATA0                          (DWC_OFFSET_MASK + 0x0204)
#define HDMITX_DWC_TX_RCRDATA1                          (DWC_OFFSET_MASK + 0x0205)
#define HDMITX_DWC_TX_BCBDATA0                          (DWC_OFFSET_MASK + 0x0206)
#define HDMITX_DWC_TX_BCBDATA1                          (DWC_OFFSET_MASK + 0x0207)
#define HDMITX_DWC_VP_STATUS                            (DWC_OFFSET_MASK + 0x0800)
#define HDMITX_DWC_VP_PR_CD                             (DWC_OFFSET_MASK + 0x0801)
#define HDMITX_DWC_VP_STUFF                             (DWC_OFFSET_MASK + 0x0802)
#define HDMITX_DWC_VP_REMAP                             (DWC_OFFSET_MASK + 0x0803)

#define HDMITX_DWC_VP_CONF                              (DWC_OFFSET_MASK + 0x0804)
 #define VP_CONF_BYPASS_EN                          (1 << 6)
 #define VP_CONF_BYPASS_SEL_VP                      (1 << 2)
 #define VP_CONF_OUTSELECTOR                        (2 << 0)
#define HDMITX_DWC_VP_MASK                              (DWC_OFFSET_MASK + 0x0807)

#define HDMITX_DWC_FC_INVIDCONF                         (DWC_OFFSET_MASK + 0x1000)
 #define FC_INVIDCONF_HDCP_KEEPOUT                  (1 << 7)
 #define FC_INVIDCONF_VSYNC_POL(x)                   (1 << 6)
 #define FC_INVIDCONF_HSYNC_POL(x)                   (1 << 5)
 #define FC_INVIDCONF_DE_POL_H                      (1 << 4)
 #define FC_INVIDCONF_DVI_HDMI_MODE                 (1 << 3)
 #define FC_INVIDCONF_VBLANK_OSC                    (1 << 1)
 #define FC_INVIDCONF_IN_VID_INTERLACED             (1 << 0)

#define HDMITX_DWC_FC_INHACTV0                          (DWC_OFFSET_MASK + 0x1001)
#define HDMITX_DWC_FC_INHACTV1                          (DWC_OFFSET_MASK + 0x1002)
#define HDMITX_DWC_FC_INHBLANK0                         (DWC_OFFSET_MASK + 0x1003)
#define HDMITX_DWC_FC_INHBLANK1                         (DWC_OFFSET_MASK + 0x1004)
#define HDMITX_DWC_FC_INVACTV0                          (DWC_OFFSET_MASK + 0x1005)
#define HDMITX_DWC_FC_INVACTV1                          (DWC_OFFSET_MASK + 0x1006)
#define HDMITX_DWC_FC_INVBLANK                          (DWC_OFFSET_MASK + 0x1007)
#define HDMITX_DWC_FC_HSYNCINDELAY0                     (DWC_OFFSET_MASK + 0x1008)
#define HDMITX_DWC_FC_HSYNCINDELAY1                     (DWC_OFFSET_MASK + 0x1009)
#define HDMITX_DWC_FC_HSYNCINWIDTH0                     (DWC_OFFSET_MASK + 0x100A)
#define HDMITX_DWC_FC_HSYNCINWIDTH1                     (DWC_OFFSET_MASK + 0x100B)
#define HDMITX_DWC_FC_VSYNCINDELAY                      (DWC_OFFSET_MASK + 0x100C)
#define HDMITX_DWC_FC_VSYNCINWIDTH                      (DWC_OFFSET_MASK + 0x100D)
#define HDMITX_DWC_FC_INFREQ0                           (DWC_OFFSET_MASK + 0x100E)
#define HDMITX_DWC_FC_INFREQ1                           (DWC_OFFSET_MASK + 0x100F)
#define HDMITX_DWC_FC_INFREQ2                           (DWC_OFFSET_MASK + 0x1010)
#define HDMITX_DWC_FC_CTRLDUR                           (DWC_OFFSET_MASK + 0x1011)
#define HDMITX_DWC_FC_EXCTRLDUR                         (DWC_OFFSET_MASK + 0x1012)
#define HDMITX_DWC_FC_EXCTRLSPAC                        (DWC_OFFSET_MASK + 0x1013)
#define HDMITX_DWC_FC_CH0PREAM                          (DWC_OFFSET_MASK + 0x1014)
#define HDMITX_DWC_FC_CH1PREAM                          (DWC_OFFSET_MASK + 0x1015)
#define HDMITX_DWC_FC_CH2PREAM                          (DWC_OFFSET_MASK + 0x1016)
#define HDMITX_DWC_FC_AVICONF3                          (DWC_OFFSET_MASK + 0x1017)
#define HDMITX_DWC_FC_GCP                               (DWC_OFFSET_MASK + 0x1018)

#define HDMITX_DWC_FC_AVICONF0                          (DWC_OFFSET_MASK + 0x1019)
 #define FC_AVICONF0_A0                         (1 << 6)
 #define FC_AVICONF0_RGB                        (0 << 0)
 #define FC_AVICONF0_444                        (2 << 0)

#define HDMITX_DWC_FC_AVICONF1                          (DWC_OFFSET_MASK + 0x101A)
 #define FC_AVICONF1_C1C0(x)                    (x << 6)
 #define FC_AVICONF1_M1M0(x)                    (x << 4)
 #define FC_AVICONF1_R3R0                       (0x8 << 0)


#define HDMITX_DWC_FC_AVICONF2                          (DWC_OFFSET_MASK + 0x101B)
#define HDMITX_DWC_FC_AVIVID                            (DWC_OFFSET_MASK + 0x101C)
#define HDMITX_DWC_FC_AVIETB0                           (DWC_OFFSET_MASK + 0x101D)
#define HDMITX_DWC_FC_AVIETB1                           (DWC_OFFSET_MASK + 0x101E)
#define HDMITX_DWC_FC_AVISBB0                           (DWC_OFFSET_MASK + 0x101F)
#define HDMITX_DWC_FC_AVISBB1                           (DWC_OFFSET_MASK + 0x1020)
#define HDMITX_DWC_FC_AVIELB0                           (DWC_OFFSET_MASK + 0x1021)
#define HDMITX_DWC_FC_AVIELB1                           (DWC_OFFSET_MASK + 0x1022)
#define HDMITX_DWC_FC_AVISRB0                           (DWC_OFFSET_MASK + 0x1023)
#define HDMITX_DWC_FC_AVISRB1                           (DWC_OFFSET_MASK + 0x1024)
#define HDMITX_DWC_FC_AUDICONF0                         (DWC_OFFSET_MASK + 0x1025)
#define HDMITX_DWC_FC_AUDICONF1                         (DWC_OFFSET_MASK + 0x1026)
#define HDMITX_DWC_FC_AUDICONF2                         (DWC_OFFSET_MASK + 0x1027)
#define HDMITX_DWC_FC_AUDICONF3                         (DWC_OFFSET_MASK + 0x1028)
#define HDMITX_DWC_FC_VSDIEEEID0                        (DWC_OFFSET_MASK + 0x1029)
#define HDMITX_DWC_FC_VSDSIZE                           (DWC_OFFSET_MASK + 0x102A)
#define HDMITX_DWC_FC_VSDIEEEID1                        (DWC_OFFSET_MASK + 0x1030)
#define HDMITX_DWC_FC_VSDIEEEID2                        (DWC_OFFSET_MASK + 0x1031)
#define HDMITX_DWC_FC_VSDPAYLOAD0                       (DWC_OFFSET_MASK + 0x1032)
#define HDMITX_DWC_FC_VSDPAYLOAD1                       (DWC_OFFSET_MASK + 0x1033)
#define HDMITX_DWC_FC_VSDPAYLOAD2                       (DWC_OFFSET_MASK + 0x1034)
#define HDMITX_DWC_FC_VSDPAYLOAD3                       (DWC_OFFSET_MASK + 0x1035)
#define HDMITX_DWC_FC_VSDPAYLOAD4                       (DWC_OFFSET_MASK + 0x1036)
#define HDMITX_DWC_FC_VSDPAYLOAD5                       (DWC_OFFSET_MASK + 0x1037)
#define HDMITX_DWC_FC_VSDPAYLOAD6                       (DWC_OFFSET_MASK + 0x1038)
#define HDMITX_DWC_FC_VSDPAYLOAD7                       (DWC_OFFSET_MASK + 0x1039)
#define HDMITX_DWC_FC_VSDPAYLOAD8                       (DWC_OFFSET_MASK + 0x103A)
#define HDMITX_DWC_FC_VSDPAYLOAD9                       (DWC_OFFSET_MASK + 0x103B)
#define HDMITX_DWC_FC_VSDPAYLOAD10                      (DWC_OFFSET_MASK + 0x103C)
#define HDMITX_DWC_FC_VSDPAYLOAD11                      (DWC_OFFSET_MASK + 0x103D)
#define HDMITX_DWC_FC_VSDPAYLOAD12                      (DWC_OFFSET_MASK + 0x103E)
#define HDMITX_DWC_FC_VSDPAYLOAD13                      (DWC_OFFSET_MASK + 0x103F)
#define HDMITX_DWC_FC_VSDPAYLOAD14                      (DWC_OFFSET_MASK + 0x1040)
#define HDMITX_DWC_FC_VSDPAYLOAD15                      (DWC_OFFSET_MASK + 0x1041)
#define HDMITX_DWC_FC_VSDPAYLOAD16                      (DWC_OFFSET_MASK + 0x1042)
#define HDMITX_DWC_FC_VSDPAYLOAD17                      (DWC_OFFSET_MASK + 0x1043)
#define HDMITX_DWC_FC_VSDPAYLOAD18                      (DWC_OFFSET_MASK + 0x1044)
#define HDMITX_DWC_FC_VSDPAYLOAD19                      (DWC_OFFSET_MASK + 0x1045)
#define HDMITX_DWC_FC_VSDPAYLOAD20                      (DWC_OFFSET_MASK + 0x1046)
#define HDMITX_DWC_FC_VSDPAYLOAD21                      (DWC_OFFSET_MASK + 0x1047)
#define HDMITX_DWC_FC_VSDPAYLOAD22                      (DWC_OFFSET_MASK + 0x1048)
#define HDMITX_DWC_FC_VSDPAYLOAD23                      (DWC_OFFSET_MASK + 0x1049)
#define HDMITX_DWC_FC_SPDVENDORNAME0                    (DWC_OFFSET_MASK + 0x104A)
#define HDMITX_DWC_FC_SPDVENDORNAME1                    (DWC_OFFSET_MASK + 0x104B)
#define HDMITX_DWC_FC_SPDVENDORNAME2                    (DWC_OFFSET_MASK + 0x104C)
#define HDMITX_DWC_FC_SPDVENDORNAME3                    (DWC_OFFSET_MASK + 0x104D)
#define HDMITX_DWC_FC_SPDVENDORNAME4                    (DWC_OFFSET_MASK + 0x104E)
#define HDMITX_DWC_FC_SPDVENDORNAME5                    (DWC_OFFSET_MASK + 0x104F)
#define HDMITX_DWC_FC_SPDVENDORNAME6                    (DWC_OFFSET_MASK + 0x1050)
#define HDMITX_DWC_FC_SPDVENDORNAME7                    (DWC_OFFSET_MASK + 0x1051)
#define HDMITX_DWC_FC_SDPPRODUCTNAME0                   (DWC_OFFSET_MASK + 0x1052)
#define HDMITX_DWC_FC_SDPPRODUCTNAME1                   (DWC_OFFSET_MASK + 0x1053)
#define HDMITX_DWC_FC_SDPPRODUCTNAME2                   (DWC_OFFSET_MASK + 0x1054)
#define HDMITX_DWC_FC_SDPPRODUCTNAME3                   (DWC_OFFSET_MASK + 0x1055)
#define HDMITX_DWC_FC_SDPPRODUCTNAME4                   (DWC_OFFSET_MASK + 0x1056)
#define HDMITX_DWC_FC_SDPPRODUCTNAME5                   (DWC_OFFSET_MASK + 0x1057)
#define HDMITX_DWC_FC_SDPPRODUCTNAME6                   (DWC_OFFSET_MASK + 0x1058)
#define HDMITX_DWC_FC_SDPPRODUCTNAME7                   (DWC_OFFSET_MASK + 0x1059)
#define HDMITX_DWC_FC_SDPPRODUCTNAME8                   (DWC_OFFSET_MASK + 0x105A)
#define HDMITX_DWC_FC_SDPPRODUCTNAME9                   (DWC_OFFSET_MASK + 0x105B)
#define HDMITX_DWC_FC_SDPPRODUCTNAME10                  (DWC_OFFSET_MASK + 0x105C)
#define HDMITX_DWC_FC_SDPPRODUCTNAME11                  (DWC_OFFSET_MASK + 0x105D)
#define HDMITX_DWC_FC_SDPPRODUCTNAME12                  (DWC_OFFSET_MASK + 0x105E)
#define HDMITX_DWC_FC_SDPPRODUCTNAME13                  (DWC_OFFSET_MASK + 0x105F)
#define HDMITX_DWC_FC_SDPPRODUCTNAME14                  (DWC_OFFSET_MASK + 0x1060)
#define HDMITX_DWC_FC_SPDPRODUCTNAME15                  (DWC_OFFSET_MASK + 0x1061)
#define HDMITX_DWC_FC_SPDDEVICEINF                      (DWC_OFFSET_MASK + 0x1062)
#define HDMITX_DWC_FC_AUDSCONF                          (DWC_OFFSET_MASK + 0x1063)
#define HDMITX_DWC_FC_AUDSSTAT                          (DWC_OFFSET_MASK + 0x1064)
#define HDMITX_DWC_FC_AUDSV                             (DWC_OFFSET_MASK + 0x1065)
#define HDMITX_DWC_FC_AUDSU                             (DWC_OFFSET_MASK + 0x1066)
#define HDMITX_DWC_FC_AUDSCHNLS0                        (DWC_OFFSET_MASK + 0x1067)
#define HDMITX_DWC_FC_AUDSCHNLS1                        (DWC_OFFSET_MASK + 0x1068)
#define HDMITX_DWC_FC_AUDSCHNLS2                        (DWC_OFFSET_MASK + 0x1069)
#define HDMITX_DWC_FC_AUDSCHNLS3                        (DWC_OFFSET_MASK + 0x106A)
#define HDMITX_DWC_FC_AUDSCHNLS4                        (DWC_OFFSET_MASK + 0x106B)
#define HDMITX_DWC_FC_AUDSCHNLS5                        (DWC_OFFSET_MASK + 0x106C)
#define HDMITX_DWC_FC_AUDSCHNLS6                        (DWC_OFFSET_MASK + 0x106D)
#define HDMITX_DWC_FC_AUDSCHNLS7                        (DWC_OFFSET_MASK + 0x106E)
#define HDMITX_DWC_FC_AUDSCHNLS8                        (DWC_OFFSET_MASK + 0x106F)
#define HDMITX_DWC_FC_DATACH0FILL                       (DWC_OFFSET_MASK + 0x1070)
#define HDMITX_DWC_FC_DATACH1FILL                       (DWC_OFFSET_MASK + 0x1071)
#define HDMITX_DWC_FC_DATACH2FILL                       (DWC_OFFSET_MASK + 0x1072)
#define HDMITX_DWC_FC_CTRLQHIGH                         (DWC_OFFSET_MASK + 0x1073)
#define HDMITX_DWC_FC_CTRLQLOW                          (DWC_OFFSET_MASK + 0x1074)
#define HDMITX_DWC_FC_ACP0                              (DWC_OFFSET_MASK + 0x1075)
#define HDMITX_DWC_FC_ACP16                             (DWC_OFFSET_MASK + 0x1082)
#define HDMITX_DWC_FC_ACP15                             (DWC_OFFSET_MASK + 0x1083)
#define HDMITX_DWC_FC_ACP14                             (DWC_OFFSET_MASK + 0x1084)
#define HDMITX_DWC_FC_ACP13                             (DWC_OFFSET_MASK + 0x1085)
#define HDMITX_DWC_FC_ACP12                             (DWC_OFFSET_MASK + 0x1086)
#define HDMITX_DWC_FC_ACP11                             (DWC_OFFSET_MASK + 0x1087)
#define HDMITX_DWC_FC_ACP10                             (DWC_OFFSET_MASK + 0x1088)
#define HDMITX_DWC_FC_ACP9                              (DWC_OFFSET_MASK + 0x1089)
#define HDMITX_DWC_FC_ACP8                              (DWC_OFFSET_MASK + 0x108A)
#define HDMITX_DWC_FC_ACP7                              (DWC_OFFSET_MASK + 0x108B)
#define HDMITX_DWC_FC_ACP6                              (DWC_OFFSET_MASK + 0x108C)
#define HDMITX_DWC_FC_ACP5                              (DWC_OFFSET_MASK + 0x108D)
#define HDMITX_DWC_FC_ACP4                              (DWC_OFFSET_MASK + 0x108E)
#define HDMITX_DWC_FC_ACP3                              (DWC_OFFSET_MASK + 0x108F)
#define HDMITX_DWC_FC_ACP2                              (DWC_OFFSET_MASK + 0x1090)
#define HDMITX_DWC_FC_ACP1                              (DWC_OFFSET_MASK + 0x1091)
#define HDMITX_DWC_FC_ISCR1_0                           (DWC_OFFSET_MASK + 0x1092)
#define HDMITX_DWC_FC_ISCR1_16                          (DWC_OFFSET_MASK + 0x1093)
#define HDMITX_DWC_FC_ISCR1_15                          (DWC_OFFSET_MASK + 0x1094)
#define HDMITX_DWC_FC_ISCR1_14                          (DWC_OFFSET_MASK + 0x1095)
#define HDMITX_DWC_FC_ISCR1_13                          (DWC_OFFSET_MASK + 0x1096)
#define HDMITX_DWC_FC_ISCR1_12                          (DWC_OFFSET_MASK + 0x1097)
#define HDMITX_DWC_FC_ISCR1_11                          (DWC_OFFSET_MASK + 0x1098)
#define HDMITX_DWC_FC_ISCR1_10                          (DWC_OFFSET_MASK + 0x1099)
#define HDMITX_DWC_FC_ISCR1_9                           (DWC_OFFSET_MASK + 0x109A)
#define HDMITX_DWC_FC_ISCR1_8                           (DWC_OFFSET_MASK + 0x109B)
#define HDMITX_DWC_FC_ISCR1_7                           (DWC_OFFSET_MASK + 0x109C)
#define HDMITX_DWC_FC_ISCR1_6                           (DWC_OFFSET_MASK + 0x109D)
#define HDMITX_DWC_FC_ISCR1_5                           (DWC_OFFSET_MASK + 0x109E)
#define HDMITX_DWC_FC_ISCR1_4                           (DWC_OFFSET_MASK + 0x109F)
#define HDMITX_DWC_FC_ISCR1_3                           (DWC_OFFSET_MASK + 0x10A0)
#define HDMITX_DWC_FC_ISCR1_2                           (DWC_OFFSET_MASK + 0x10A1)
#define HDMITX_DWC_FC_ISCR1_1                           (DWC_OFFSET_MASK + 0x10A2)
#define HDMITX_DWC_FC_ISCR0_15                          (DWC_OFFSET_MASK + 0x10A3)
#define HDMITX_DWC_FC_ISCR0_14                          (DWC_OFFSET_MASK + 0x10A4)
#define HDMITX_DWC_FC_ISCR0_13                          (DWC_OFFSET_MASK + 0x10A5)
#define HDMITX_DWC_FC_ISCR0_12                          (DWC_OFFSET_MASK + 0x10A6)
#define HDMITX_DWC_FC_ISCR0_11                          (DWC_OFFSET_MASK + 0x10A7)
#define HDMITX_DWC_FC_ISCR0_10                          (DWC_OFFSET_MASK + 0x10A8)
#define HDMITX_DWC_FC_ISCR0_9                           (DWC_OFFSET_MASK + 0x10A9)
#define HDMITX_DWC_FC_ISCR0_8                           (DWC_OFFSET_MASK + 0x10AA)
#define HDMITX_DWC_FC_ISCR0_7                           (DWC_OFFSET_MASK + 0x10AB)
#define HDMITX_DWC_FC_ISCR0_6                           (DWC_OFFSET_MASK + 0x10AC)
#define HDMITX_DWC_FC_ISCR0_5                           (DWC_OFFSET_MASK + 0x10AD)
#define HDMITX_DWC_FC_ISCR0_4                           (DWC_OFFSET_MASK + 0x10AE)
#define HDMITX_DWC_FC_ISCR0_3                           (DWC_OFFSET_MASK + 0x10AF)
#define HDMITX_DWC_FC_ISCR0_2                           (DWC_OFFSET_MASK + 0x10B0)
#define HDMITX_DWC_FC_ISCR0_1                           (DWC_OFFSET_MASK + 0x10B1)
#define HDMITX_DWC_FC_ISCR0_0                           (DWC_OFFSET_MASK + 0x10B2)
#define HDMITX_DWC_FC_DATAUTO0                          (DWC_OFFSET_MASK + 0x10B3)
#define HDMITX_DWC_FC_DATAUTO1                          (DWC_OFFSET_MASK + 0x10B4)
#define HDMITX_DWC_FC_DATAUTO2                          (DWC_OFFSET_MASK + 0x10B5)
#define HDMITX_DWC_FC_DATMAN                            (DWC_OFFSET_MASK + 0x10B6)
#define HDMITX_DWC_FC_DATAUTO3                          (DWC_OFFSET_MASK + 0x10B7)
#define HDMITX_DWC_FC_RDRB0                             (DWC_OFFSET_MASK + 0x10B8)
#define HDMITX_DWC_FC_RDRB1                             (DWC_OFFSET_MASK + 0x10B9)
#define HDMITX_DWC_FC_RDRB2                             (DWC_OFFSET_MASK + 0x10BA)
#define HDMITX_DWC_FC_RDRB3                             (DWC_OFFSET_MASK + 0x10BB)
#define HDMITX_DWC_FC_RDRB4                             (DWC_OFFSET_MASK + 0x10BC)
#define HDMITX_DWC_FC_RDRB5                             (DWC_OFFSET_MASK + 0x10BD)
#define HDMITX_DWC_FC_RDRB6                             (DWC_OFFSET_MASK + 0x10BE)
#define HDMITX_DWC_FC_RDRB7                             (DWC_OFFSET_MASK + 0x10BF)
#define HDMITX_DWC_FC_RDRB8                             (DWC_OFFSET_MASK + 0x10C0)
#define HDMITX_DWC_FC_RDRB9                             (DWC_OFFSET_MASK + 0x10C1)
#define HDMITX_DWC_FC_RDRB10                            (DWC_OFFSET_MASK + 0x10C2)
#define HDMITX_DWC_FC_RDRB11                            (DWC_OFFSET_MASK + 0x10C3)
#define HDMITX_DWC_FC_MASK0                             (DWC_OFFSET_MASK + 0x10D2)
#define HDMITX_DWC_FC_MASK1                             (DWC_OFFSET_MASK + 0x10D6)
#define HDMITX_DWC_FC_MASK2                             (DWC_OFFSET_MASK + 0x10DA)
#define HDMITX_DWC_FC_PRCONF                            (DWC_OFFSET_MASK + 0x10E0)
#define HDMITX_DWC_FC_SCRAMBLER_CTRL                    (DWC_OFFSET_MASK + 0x10E1)
#define HDMITX_DWC_FC_MULTISTREAM_CTRL                  (DWC_OFFSET_MASK + 0x10E2)
#define HDMITX_DWC_FC_PACKET_TX_EN                      (DWC_OFFSET_MASK + 0x10E3)
#define HDMITX_DWC_FC_ACTSPC_HDLR_CFG                   (DWC_OFFSET_MASK + 0x10E8)
#define HDMITX_DWC_FC_INVACT_2D_0                       (DWC_OFFSET_MASK + 0x10E9)
#define HDMITX_DWC_FC_INVACT_2D_1                       (DWC_OFFSET_MASK + 0x10EA)
#define HDMITX_DWC_FC_GMD_STAT                          (DWC_OFFSET_MASK + 0x1100)
#define HDMITX_DWC_FC_GMD_EN                            (DWC_OFFSET_MASK + 0x1101)
#define HDMITX_DWC_FC_GMD_UP                            (DWC_OFFSET_MASK + 0x1102)
#define HDMITX_DWC_FC_GMD_CONF                          (DWC_OFFSET_MASK + 0x1103)
#define HDMITX_DWC_FC_GMD_HB                            (DWC_OFFSET_MASK + 0x1104)
#define HDMITX_DWC_FC_GMD_PB0                           (DWC_OFFSET_MASK + 0x1105)
#define HDMITX_DWC_FC_GMD_PB1                           (DWC_OFFSET_MASK + 0x1106)
#define HDMITX_DWC_FC_GMD_PB2                           (DWC_OFFSET_MASK + 0x1107)
#define HDMITX_DWC_FC_GMD_PB3                           (DWC_OFFSET_MASK + 0x1108)
#define HDMITX_DWC_FC_GMD_PB4                           (DWC_OFFSET_MASK + 0x1109)
#define HDMITX_DWC_FC_GMD_PB5                           (DWC_OFFSET_MASK + 0x110A)
#define HDMITX_DWC_FC_GMD_PB6                           (DWC_OFFSET_MASK + 0x110B)
#define HDMITX_DWC_FC_GMD_PB7                           (DWC_OFFSET_MASK + 0x110C)
#define HDMITX_DWC_FC_GMD_PB8                           (DWC_OFFSET_MASK + 0x110D)
#define HDMITX_DWC_FC_GMD_PB9                           (DWC_OFFSET_MASK + 0x110E)
#define HDMITX_DWC_FC_GMD_PB10                          (DWC_OFFSET_MASK + 0x110F)
#define HDMITX_DWC_FC_GMD_PB11                          (DWC_OFFSET_MASK + 0x1110)
#define HDMITX_DWC_FC_GMD_PB12                          (DWC_OFFSET_MASK + 0x1111)
#define HDMITX_DWC_FC_GMD_PB13                          (DWC_OFFSET_MASK + 0x1112)
#define HDMITX_DWC_FC_GMD_PB14                          (DWC_OFFSET_MASK + 0x1113)
#define HDMITX_DWC_FC_GMD_PB15                          (DWC_OFFSET_MASK + 0x1114)
#define HDMITX_DWC_FC_GMD_PB16                          (DWC_OFFSET_MASK + 0x1115)
#define HDMITX_DWC_FC_GMD_PB17                          (DWC_OFFSET_MASK + 0x1116)
#define HDMITX_DWC_FC_GMD_PB18                          (DWC_OFFSET_MASK + 0x1117)
#define HDMITX_DWC_FC_GMD_PB19                          (DWC_OFFSET_MASK + 0x1118)
#define HDMITX_DWC_FC_GMD_PB20                          (DWC_OFFSET_MASK + 0x1119)
#define HDMITX_DWC_FC_GMD_PB21                          (DWC_OFFSET_MASK + 0x111A)
#define HDMITX_DWC_FC_GMD_PB22                          (DWC_OFFSET_MASK + 0x111B)
#define HDMITX_DWC_FC_GMD_PB23                          (DWC_OFFSET_MASK + 0x111C)
#define HDMITX_DWC_FC_GMD_PB24                          (DWC_OFFSET_MASK + 0x111D)
#define HDMITX_DWC_FC_GMD_PB25                          (DWC_OFFSET_MASK + 0x111E)
#define HDMITX_DWC_FC_GMD_PB26                          (DWC_OFFSET_MASK + 0x111F)
#define HDMITX_DWC_FC_GMD_PB27                          (DWC_OFFSET_MASK + 0x1120)
#define HDMITX_DWC_FC_AMP_HB01                          (DWC_OFFSET_MASK + 0x1128)
#define HDMITX_DWC_FC_AMP_HB02                          (DWC_OFFSET_MASK + 0x1129)
#define HDMITX_DWC_FC_AMP_PB00                          (DWC_OFFSET_MASK + 0x112A)
#define HDMITX_DWC_FC_AMP_PB01                          (DWC_OFFSET_MASK + 0x112B)
#define HDMITX_DWC_FC_AMP_PB02                          (DWC_OFFSET_MASK + 0x112C)
#define HDMITX_DWC_FC_AMP_PB03                          (DWC_OFFSET_MASK + 0x112D)
#define HDMITX_DWC_FC_AMP_PB04                          (DWC_OFFSET_MASK + 0x112E)
#define HDMITX_DWC_FC_AMP_PB05                          (DWC_OFFSET_MASK + 0x112F)
#define HDMITX_DWC_FC_AMP_PB06                          (DWC_OFFSET_MASK + 0x1130)
#define HDMITX_DWC_FC_AMP_PB07                          (DWC_OFFSET_MASK + 0x1131)
#define HDMITX_DWC_FC_AMP_PB08                          (DWC_OFFSET_MASK + 0x1132)
#define HDMITX_DWC_FC_AMP_PB09                          (DWC_OFFSET_MASK + 0x1133)
#define HDMITX_DWC_FC_AMP_PB10                          (DWC_OFFSET_MASK + 0x1134)
#define HDMITX_DWC_FC_AMP_PB11                          (DWC_OFFSET_MASK + 0x1135)
#define HDMITX_DWC_FC_AMP_PB12                          (DWC_OFFSET_MASK + 0x1136)
#define HDMITX_DWC_FC_AMP_PB13                          (DWC_OFFSET_MASK + 0x1137)
#define HDMITX_DWC_FC_AMP_PB14                          (DWC_OFFSET_MASK + 0x1138)
#define HDMITX_DWC_FC_AMP_PB15                          (DWC_OFFSET_MASK + 0x1139)
#define HDMITX_DWC_FC_AMP_PB16                          (DWC_OFFSET_MASK + 0x113A)
#define HDMITX_DWC_FC_AMP_PB17                          (DWC_OFFSET_MASK + 0x113B)
#define HDMITX_DWC_FC_AMP_PB18                          (DWC_OFFSET_MASK + 0x113C)
#define HDMITX_DWC_FC_AMP_PB19                          (DWC_OFFSET_MASK + 0x113D)
#define HDMITX_DWC_FC_AMP_PB20                          (DWC_OFFSET_MASK + 0x113E)
#define HDMITX_DWC_FC_AMP_PB21                          (DWC_OFFSET_MASK + 0x113F)
#define HDMITX_DWC_FC_AMP_PB22                          (DWC_OFFSET_MASK + 0x1140)
#define HDMITX_DWC_FC_AMP_PB23                          (DWC_OFFSET_MASK + 0x1141)
#define HDMITX_DWC_FC_AMP_PB24                          (DWC_OFFSET_MASK + 0x1142)
#define HDMITX_DWC_FC_AMP_PB25                          (DWC_OFFSET_MASK + 0x1143)
#define HDMITX_DWC_FC_AMP_PB26                          (DWC_OFFSET_MASK + 0x1144)
#define HDMITX_DWC_FC_AMP_PB27                          (DWC_OFFSET_MASK + 0x1145)
#define HDMITX_DWC_FC_NVBI_HB01                         (DWC_OFFSET_MASK + 0x1148)
#define HDMITX_DWC_FC_NVBI_HB02                         (DWC_OFFSET_MASK + 0x1149)
#define HDMITX_DWC_FC_NVBI_PB01                         (DWC_OFFSET_MASK + 0x114A)
#define HDMITX_DWC_FC_NVBI_PB02                         (DWC_OFFSET_MASK + 0x114B)
#define HDMITX_DWC_FC_NVBI_PB03                         (DWC_OFFSET_MASK + 0x114C)
#define HDMITX_DWC_FC_NVBI_PB04                         (DWC_OFFSET_MASK + 0x114D)
#define HDMITX_DWC_FC_NVBI_PB05                         (DWC_OFFSET_MASK + 0x114E)
#define HDMITX_DWC_FC_NVBI_PB06                         (DWC_OFFSET_MASK + 0x114F)
#define HDMITX_DWC_FC_NVBI_PB07                         (DWC_OFFSET_MASK + 0x1150)
#define HDMITX_DWC_FC_NVBI_PB08                         (DWC_OFFSET_MASK + 0x1151)
#define HDMITX_DWC_FC_NVBI_PB09                         (DWC_OFFSET_MASK + 0x1152)
#define HDMITX_DWC_FC_NVBI_PB10                         (DWC_OFFSET_MASK + 0x1153)
#define HDMITX_DWC_FC_NVBI_PB11                         (DWC_OFFSET_MASK + 0x1154)
#define HDMITX_DWC_FC_NVBI_PB12                         (DWC_OFFSET_MASK + 0x1155)
#define HDMITX_DWC_FC_NVBI_PB13                         (DWC_OFFSET_MASK + 0x1156)
#define HDMITX_DWC_FC_NVBI_PB14                         (DWC_OFFSET_MASK + 0x1157)
#define HDMITX_DWC_FC_NVBI_PB15                         (DWC_OFFSET_MASK + 0x1158)
#define HDMITX_DWC_FC_NVBI_PB16                         (DWC_OFFSET_MASK + 0x1159)
#define HDMITX_DWC_FC_NVBI_PB17                         (DWC_OFFSET_MASK + 0x115A)
#define HDMITX_DWC_FC_NVBI_PB18                         (DWC_OFFSET_MASK + 0x115B)
#define HDMITX_DWC_FC_NVBI_PB19                         (DWC_OFFSET_MASK + 0x115C)
#define HDMITX_DWC_FC_NVBI_PB20                         (DWC_OFFSET_MASK + 0x115D)
#define HDMITX_DWC_FC_NVBI_PB21                         (DWC_OFFSET_MASK + 0x115E)
#define HDMITX_DWC_FC_NVBI_PB22                         (DWC_OFFSET_MASK + 0x115F)
#define HDMITX_DWC_FC_NVBI_PB23                         (DWC_OFFSET_MASK + 0x1160)
#define HDMITX_DWC_FC_NVBI_PB24                         (DWC_OFFSET_MASK + 0x1161)
#define HDMITX_DWC_FC_NVBI_PB25                         (DWC_OFFSET_MASK + 0x1162)
#define HDMITX_DWC_FC_NVBI_PB26                         (DWC_OFFSET_MASK + 0x1163)
#define HDMITX_DWC_FC_NVBI_PB27                         (DWC_OFFSET_MASK + 0x1164)
#define HDMITX_DWC_FC_DBGFORCE                          (DWC_OFFSET_MASK + 0x1200)
#define HDMITX_DWC_FC_DBGAUD0CH0                        (DWC_OFFSET_MASK + 0x1201)
#define HDMITX_DWC_FC_DBGAUD1CH0                        (DWC_OFFSET_MASK + 0x1202)
#define HDMITX_DWC_FC_DBGAUD2CH0                        (DWC_OFFSET_MASK + 0x1203)
#define HDMITX_DWC_FC_DBGAUD0CH1                        (DWC_OFFSET_MASK + 0x1204)
#define HDMITX_DWC_FC_DBGAUD1CH1                        (DWC_OFFSET_MASK + 0x1205)
#define HDMITX_DWC_FC_DBGAUD2CH1                        (DWC_OFFSET_MASK + 0x1206)
#define HDMITX_DWC_FC_DBGAUD0CH2                        (DWC_OFFSET_MASK + 0x1207)
#define HDMITX_DWC_FC_DBGAUD1CH2                        (DWC_OFFSET_MASK + 0x1208)
#define HDMITX_DWC_FC_DBGAUD2CH2                        (DWC_OFFSET_MASK + 0x1209)
#define HDMITX_DWC_FC_DBGAUD0CH3                        (DWC_OFFSET_MASK + 0x120A)
#define HDMITX_DWC_FC_DBGAUD1CH3                        (DWC_OFFSET_MASK + 0x120B)
#define HDMITX_DWC_FC_DBGAUD2CH3                        (DWC_OFFSET_MASK + 0x120C)
#define HDMITX_DWC_FC_DBGAUD0CH4                        (DWC_OFFSET_MASK + 0x120D)
#define HDMITX_DWC_FC_DBGAUD1CH4                        (DWC_OFFSET_MASK + 0x120E)
#define HDMITX_DWC_FC_DBGAUD2CH4                        (DWC_OFFSET_MASK + 0x120F)
#define HDMITX_DWC_FC_DBGAUD0CH5                        (DWC_OFFSET_MASK + 0x1210)
#define HDMITX_DWC_FC_DBGAUD1CH5                        (DWC_OFFSET_MASK + 0x1211)
#define HDMITX_DWC_FC_DBGAUD2CH5                        (DWC_OFFSET_MASK + 0x1212)
#define HDMITX_DWC_FC_DBGAUD0CH6                        (DWC_OFFSET_MASK + 0x1213)
#define HDMITX_DWC_FC_DBGAUD1CH6                        (DWC_OFFSET_MASK + 0x1214)
#define HDMITX_DWC_FC_DBGAUD2CH6                        (DWC_OFFSET_MASK + 0x1215)
#define HDMITX_DWC_FC_DBGAUD0CH7                        (DWC_OFFSET_MASK + 0x1216)
#define HDMITX_DWC_FC_DBGAUD1CH7                        (DWC_OFFSET_MASK + 0x1217)
#define HDMITX_DWC_FC_DBGAUD2CH7                        (DWC_OFFSET_MASK + 0x1218)
#define HDMITX_DWC_FC_DBGTMDS0                          (DWC_OFFSET_MASK + 0x1219)
#define HDMITX_DWC_FC_DBGTMDS1                          (DWC_OFFSET_MASK + 0x121A)
#define HDMITX_DWC_FC_DBGTMDS2                          (DWC_OFFSET_MASK + 0x121B)
#define HDMITX_DWC_PHY_CONF0                            (DWC_OFFSET_MASK + 0x3000)
#define HDMITX_DWC_PHY_TST0                             (DWC_OFFSET_MASK + 0x3001)
#define HDMITX_DWC_PHY_TST1                             (DWC_OFFSET_MASK + 0x3002)
#define HDMITX_DWC_PHY_TST2                             (DWC_OFFSET_MASK + 0x3003)
#define HDMITX_DWC_PHY_STAT0                            (DWC_OFFSET_MASK + 0x3004)
#define HDMITX_DWC_PHY_INT0                             (DWC_OFFSET_MASK + 0x3005)
#define HDMITX_DWC_PHY_MASK0                            (DWC_OFFSET_MASK + 0x3006)
#define HDMITX_DWC_PHY_POL0                             (DWC_OFFSET_MASK + 0x3007)
#define HDMITX_DWC_I2CM_PHY_SLAVE                       (DWC_OFFSET_MASK + 0x3020)
#define HDMITX_DWC_I2CM_PHY_ADDRESS                     (DWC_OFFSET_MASK + 0x3021)
#define HDMITX_DWC_I2CM_PHY_DATAO_1                     (DWC_OFFSET_MASK + 0x3022)
#define HDMITX_DWC_I2CM_PHY_DATAO_0                     (DWC_OFFSET_MASK + 0x3023)
#define HDMITX_DWC_I2CM_PHY_DATAI_1                     (DWC_OFFSET_MASK + 0x3024)
#define HDMITX_DWC_I2CM_PHY_DATAI_0                     (DWC_OFFSET_MASK + 0x3025)
#define HDMITX_DWC_I2CM_PHY_OPERATION                   (DWC_OFFSET_MASK + 0x3026)
#define HDMITX_DWC_I2CM_PHY_INT                         (DWC_OFFSET_MASK + 0x3027)
#define HDMITX_DWC_I2CM_PHY_CTLINT                      (DWC_OFFSET_MASK + 0x3028)
#define HDMITX_DWC_I2CM_PHY_DIV                         (DWC_OFFSET_MASK + 0x3029)
#define HDMITX_DWC_I2CM_PHY_SOFTRSTZ                    (DWC_OFFSET_MASK + 0x302A)
#define HDMITX_DWC_I2CM_PHY_SS_SCL_HCNT_1               (DWC_OFFSET_MASK + 0x302B)
#define HDMITX_DWC_I2CM_PHY_SS_SCL_HCNT_0               (DWC_OFFSET_MASK + 0x302C)
#define HDMITX_DWC_I2CM_PHY_SS_SCL_LCNT_1               (DWC_OFFSET_MASK + 0x302D)
#define HDMITX_DWC_I2CM_PHY_SS_SCL_LCNT_0               (DWC_OFFSET_MASK + 0x302E)
#define HDMITX_DWC_I2CM_PHY_FS_SCL_HCNT_1               (DWC_OFFSET_MASK + 0x302F)
#define HDMITX_DWC_I2CM_PHY_FS_SCL_HCNT_0               (DWC_OFFSET_MASK + 0x3030)
#define HDMITX_DWC_I2CM_PHY_FS_SCL_LCNT_1               (DWC_OFFSET_MASK + 0x3031)
#define HDMITX_DWC_I2CM_PHY_FS_SCL_LCNT_0               (DWC_OFFSET_MASK + 0x3032)
#define HDMITX_DWC_I2CM_PHY_SDA_HOLD                    (DWC_OFFSET_MASK + 0x3033)
#define HDMITX_DWC_AUD_CONF0                            (DWC_OFFSET_MASK + 0x3100)
#define HDMITX_DWC_AUD_CONF1                            (DWC_OFFSET_MASK + 0x3101)
#define HDMITX_DWC_AUD_INT                              (DWC_OFFSET_MASK + 0x3102)
#define HDMITX_DWC_AUD_CONF2                            (DWC_OFFSET_MASK + 0x3103)
#define HDMITX_DWC_AUD_INT1                             (DWC_OFFSET_MASK + 0x3104)
#define HDMITX_DWC_AUD_N1                               (DWC_OFFSET_MASK + 0x3200)
#define HDMITX_DWC_AUD_N2                               (DWC_OFFSET_MASK + 0x3201)
#define HDMITX_DWC_AUD_N3                               (DWC_OFFSET_MASK + 0x3202)
#define HDMITX_DWC_AUD_CTS1                             (DWC_OFFSET_MASK + 0x3203)
#define HDMITX_DWC_AUD_CTS2                             (DWC_OFFSET_MASK + 0x3204)
#define HDMITX_DWC_AUD_CTS3                             (DWC_OFFSET_MASK + 0x3205)
#define HDMITX_DWC_AUD_INPUTCLKFS                       (DWC_OFFSET_MASK + 0x3206)
#define HDMITX_DWC_AUD_SPDIF0                           (DWC_OFFSET_MASK + 0x3300)
#define HDMITX_DWC_AUD_SPDIF1                           (DWC_OFFSET_MASK + 0x3301)
#define HDMITX_DWC_AUD_SPDIFINT                         (DWC_OFFSET_MASK + 0x3302)
#define HDMITX_DWC_AUD_SPDIFINT1                        (DWC_OFFSET_MASK + 0x3303)
#define HDMITX_DWC_MC_CLKDIS                            (DWC_OFFSET_MASK + 0x4001)
#define HDMITX_DWC_MC_SWRSTZREQ                         (DWC_OFFSET_MASK + 0x4002)
#define HDMITX_DWC_MC_OPCTRL                            (DWC_OFFSET_MASK + 0x4003)

#define HDMITX_DWC_MC_FLOWCTRL                          (DWC_OFFSET_MASK + 0x4004)
 #define MC_FLOWCTRL_ENB_CSC                    (1 << 0)
 #define MC_FLOWCTRL_BYPASS_CSC                 (0 << 0)

#define HDMITX_DWC_MC_PHYRSTZ                           (DWC_OFFSET_MASK + 0x4005)
#define HDMITX_DWC_MC_LOCKONCLOCK                       (DWC_OFFSET_MASK + 0x4006)
#define HDMITX_DWC_CSC_CFG                              (DWC_OFFSET_MASK + 0x4100)

#define HDMITX_DWC_CSC_SCALE                            (DWC_OFFSET_MASK + 0x4101)
 #define CSC_SCALE_COLOR_DEPTH(x)               (x << 4)
 #define CSC_SCALE_CSCSCALE(x)                  (x << 0)

#define HDMITX_DWC_CSC_COEF_A1_MSB                      (DWC_OFFSET_MASK + 0x4102)
#define HDMITX_DWC_CSC_COEF_A1_LSB                      (DWC_OFFSET_MASK + 0x4103)
#define HDMITX_DWC_CSC_COEF_A2_MSB                      (DWC_OFFSET_MASK + 0x4104)
#define HDMITX_DWC_CSC_COEF_A2_LSB                      (DWC_OFFSET_MASK + 0x4105)
#define HDMITX_DWC_CSC_COEF_A3_MSB                      (DWC_OFFSET_MASK + 0x4106)
#define HDMITX_DWC_CSC_COEF_A3_LSB                      (DWC_OFFSET_MASK + 0x4107)
#define HDMITX_DWC_CSC_COEF_A4_MSB                      (DWC_OFFSET_MASK + 0x4108)
#define HDMITX_DWC_CSC_COEF_A4_LSB                      (DWC_OFFSET_MASK + 0x4109)
#define HDMITX_DWC_CSC_COEF_B1_MSB                      (DWC_OFFSET_MASK + 0x410A)
#define HDMITX_DWC_CSC_COEF_B1_LSB                      (DWC_OFFSET_MASK + 0x410B)
#define HDMITX_DWC_CSC_COEF_B2_MSB                      (DWC_OFFSET_MASK + 0x410C)
#define HDMITX_DWC_CSC_COEF_B2_LSB                      (DWC_OFFSET_MASK + 0x410D)
#define HDMITX_DWC_CSC_COEF_B3_MSB                      (DWC_OFFSET_MASK + 0x410E)
#define HDMITX_DWC_CSC_COEF_B3_LSB                      (DWC_OFFSET_MASK + 0x410F)
#define HDMITX_DWC_CSC_COEF_B4_MSB                      (DWC_OFFSET_MASK + 0x4110)
#define HDMITX_DWC_CSC_COEF_B4_LSB                      (DWC_OFFSET_MASK + 0x4111)
#define HDMITX_DWC_CSC_COEF_C1_MSB                      (DWC_OFFSET_MASK + 0x4112)
#define HDMITX_DWC_CSC_COEF_C1_LSB                      (DWC_OFFSET_MASK + 0x4113)
#define HDMITX_DWC_CSC_COEF_C2_MSB                      (DWC_OFFSET_MASK + 0x4114)
#define HDMITX_DWC_CSC_COEF_C2_LSB                      (DWC_OFFSET_MASK + 0x4115)
#define HDMITX_DWC_CSC_COEF_C3_MSB                      (DWC_OFFSET_MASK + 0x4116)
#define HDMITX_DWC_CSC_COEF_C3_LSB                      (DWC_OFFSET_MASK + 0x4117)
#define HDMITX_DWC_CSC_COEF_C4_MSB                      (DWC_OFFSET_MASK + 0x4118)
#define HDMITX_DWC_CSC_COEF_C4_LSB                      (DWC_OFFSET_MASK + 0x4119)
#define HDMITX_DWC_CSC_LIMIT_UP_MSB                     (DWC_OFFSET_MASK + 0x411A)
#define HDMITX_DWC_CSC_LIMIT_UP_LSB                     (DWC_OFFSET_MASK + 0x411B)
#define HDMITX_DWC_CSC_LIMIT_DN_MSB                     (DWC_OFFSET_MASK + 0x411C)
#define HDMITX_DWC_CSC_LIMIT_DN_LSB                     (DWC_OFFSET_MASK + 0x411D)
#define HDMITX_DWC_A_HDCPCFG0                           (DWC_SEC_OFFSET_MASK + 0x5000)
#define HDMITX_DWC_A_HDCPCFG1                           (DWC_SEC_OFFSET_MASK + 0x5001)
#define HDMITX_DWC_A_HDCPOBS0                           (DWC_OFFSET_MASK + 0x5002)
#define HDMITX_DWC_A_HDCPOBS1                           (DWC_OFFSET_MASK + 0x5003)
#define HDMITX_DWC_A_HDCPOBS2                           (DWC_OFFSET_MASK + 0x5004)
#define HDMITX_DWC_A_HDCPOBS3                           (DWC_OFFSET_MASK + 0x5005)
#define HDMITX_DWC_A_APIINTCLR                          (DWC_OFFSET_MASK + 0x5006)
#define HDMITX_DWC_A_APIINTSTAT                         (DWC_OFFSET_MASK + 0x5007)
#define HDMITX_DWC_A_APIINTMSK                          (DWC_OFFSET_MASK + 0x5008)
#define HDMITX_DWC_A_VIDPOLCFG                          (DWC_OFFSET_MASK + 0x5009)
#define HDMITX_DWC_A_OESSWCFG                           (DWC_OFFSET_MASK + 0x500A)
#define HDMITX_DWC_A_COREVERLSB                         (DWC_OFFSET_MASK + 0x5014)
#define HDMITX_DWC_A_COREVERMSB                         (DWC_OFFSET_MASK + 0x5015)
#define HDMITX_DWC_A_KSVMEMCTRL                         (DWC_OFFSET_MASK + 0x5016)
#define HDMITX_DWC_HDCP_BSTATUS_0                       (DWC_OFFSET_MASK + 0x5020)
#define HDMITX_DWC_HDCP_BSTATUS_1                       (DWC_OFFSET_MASK + 0x5021)
#define HDMITX_DWC_HDCP_M0_0                            (DWC_OFFSET_MASK + 0x5022)
#define HDMITX_DWC_HDCP_M0_1                            (DWC_OFFSET_MASK + 0x5023)
#define HDMITX_DWC_HDCP_M0_2                            (DWC_OFFSET_MASK + 0x5024)
#define HDMITX_DWC_HDCP_M0_3                            (DWC_OFFSET_MASK + 0x5025)
#define HDMITX_DWC_HDCP_M0_4                            (DWC_OFFSET_MASK + 0x5026)
#define HDMITX_DWC_HDCP_M0_5                            (DWC_OFFSET_MASK + 0x5027)
#define HDMITX_DWC_HDCP_M0_6                            (DWC_OFFSET_MASK + 0x5028)
#define HDMITX_DWC_HDCP_M0_7                            (DWC_OFFSET_MASK + 0x5029)
#define HDMITX_DWC_HDCP_KSV                             (DWC_OFFSET_MASK + 0x502A)
#define HDMITX_DWC_HDCP_VH                              (DWC_OFFSET_MASK + 0x52A5)
#define HDMITX_DWC_HDCP_REVOC_SIZE_0                    (DWC_OFFSET_MASK + 0x52B9)
#define HDMITX_DWC_HDCP_REVOC_SIZE_1                    (DWC_OFFSET_MASK + 0x52BA)
#define HDMITX_DWC_HDCP_REVOC_LIST                      (DWC_OFFSET_MASK + 0x52BB)
#define HDMITX_DWC_HDCPREG_BKSV0                        (DWC_OFFSET_MASK + 0x7800)
#define HDMITX_DWC_HDCPREG_BKSV1                        (DWC_OFFSET_MASK + 0x7801)
#define HDMITX_DWC_HDCPREG_BKSV2                        (DWC_OFFSET_MASK + 0x7802)
#define HDMITX_DWC_HDCPREG_BKSV3                        (DWC_OFFSET_MASK + 0x7803)
#define HDMITX_DWC_HDCPREG_BKSV4                        (DWC_OFFSET_MASK + 0x7804)
#define HDMITX_DWC_HDCPREG_ANCONF                       (DWC_OFFSET_MASK + 0x7805)
#define HDMITX_DWC_HDCPREG_AN0                          (DWC_OFFSET_MASK + 0x7806)
#define HDMITX_DWC_HDCPREG_AN1                          (DWC_OFFSET_MASK + 0x7807)
#define HDMITX_DWC_HDCPREG_AN2                          (DWC_OFFSET_MASK + 0x7808)
#define HDMITX_DWC_HDCPREG_AN3                          (DWC_OFFSET_MASK + 0x7809)
#define HDMITX_DWC_HDCPREG_AN4                          (DWC_OFFSET_MASK + 0x780A)
#define HDMITX_DWC_HDCPREG_AN5                          (DWC_OFFSET_MASK + 0x780B)
#define HDMITX_DWC_HDCPREG_AN6                          (DWC_OFFSET_MASK + 0x780C)
#define HDMITX_DWC_HDCPREG_AN7                          (DWC_OFFSET_MASK + 0x780D)
#define HDMITX_DWC_HDCPREG_RMLCTL                       (DWC_OFFSET_MASK + 0x780E)
#define HDMITX_DWC_HDCPREG_RMLSTS                       (DWC_OFFSET_MASK + 0x780F)
#define HDMITX_DWC_HDCPREG_SEED0                        (DWC_SEC_OFFSET_MASK + 0x7810)
#define HDMITX_DWC_HDCPREG_SEED1                        (DWC_SEC_OFFSET_MASK + 0x7811)
#define HDMITX_DWC_HDCPREG_DPK0                         (DWC_SEC_OFFSET_MASK + 0x7812)
#define HDMITX_DWC_HDCPREG_DPK1                         (DWC_SEC_OFFSET_MASK + 0x7813)
#define HDMITX_DWC_HDCPREG_DPK2                         (DWC_SEC_OFFSET_MASK + 0x7814)
#define HDMITX_DWC_HDCPREG_DPK3                         (DWC_SEC_OFFSET_MASK + 0x7815)
#define HDMITX_DWC_HDCPREG_DPK4                         (DWC_SEC_OFFSET_MASK + 0x7816)
#define HDMITX_DWC_HDCPREG_DPK5                         (DWC_SEC_OFFSET_MASK + 0x7817)
#define HDMITX_DWC_HDCPREG_DPK6                         (DWC_SEC_OFFSET_MASK + 0x7818)
#define HDMITX_DWC_HDCP22REG_ID                         (DWC_OFFSET_MASK + 0x7900)
#define HDMITX_DWC_HDCP22REG_CTRL                       (DWC_SEC_OFFSET_MASK + 0x7904)
#define HDMITX_DWC_HDCP22REG_CTRL1                      (DWC_OFFSET_MASK + 0x7905)
#define HDMITX_DWC_HDCP22REG_STS                        (DWC_OFFSET_MASK + 0x7908)
#define HDMITX_DWC_HDCP22REG_MASK                       (DWC_OFFSET_MASK + 0x790C)
#define HDMITX_DWC_HDCP22REG_STAT                       (DWC_OFFSET_MASK + 0x790D)
#define HDMITX_DWC_HDCP22REG_MUTE                       (DWC_OFFSET_MASK + 0x790E)
#define HDMITX_DWC_CEC_CTRL                             (DWC_OFFSET_MASK + 0x7D00)
#define HDMITX_DWC_CEC_INTR_MASK                        (DWC_OFFSET_MASK + 0x7D02)
#define HDMITX_DWC_CEC_LADD_LOW                         (DWC_OFFSET_MASK + 0x7D05)
#define HDMITX_DWC_CEC_LADD_HIGH                        (DWC_OFFSET_MASK + 0x7D06)
#define HDMITX_DWC_CEC_TX_CNT                           (DWC_OFFSET_MASK + 0x7D07)
#define HDMITX_DWC_CEC_RX_CNT                           (DWC_OFFSET_MASK + 0x7D08)
#define HDMITX_DWC_CEC_TX_DATA00                        (DWC_OFFSET_MASK + 0x7D10)
#define HDMITX_DWC_CEC_TX_DATA01                        (DWC_OFFSET_MASK + 0x7D11)
#define HDMITX_DWC_CEC_TX_DATA02                        (DWC_OFFSET_MASK + 0x7D12)
#define HDMITX_DWC_CEC_TX_DATA03                        (DWC_OFFSET_MASK + 0x7D13)
#define HDMITX_DWC_CEC_TX_DATA04                        (DWC_OFFSET_MASK + 0x7D14)
#define HDMITX_DWC_CEC_TX_DATA05                        (DWC_OFFSET_MASK + 0x7D15)
#define HDMITX_DWC_CEC_TX_DATA06                        (DWC_OFFSET_MASK + 0x7D16)
#define HDMITX_DWC_CEC_TX_DATA07                        (DWC_OFFSET_MASK + 0x7D17)
#define HDMITX_DWC_CEC_TX_DATA08                        (DWC_OFFSET_MASK + 0x7D18)
#define HDMITX_DWC_CEC_TX_DATA09                        (DWC_OFFSET_MASK + 0x7D19)
#define HDMITX_DWC_CEC_TX_DATA10                        (DWC_OFFSET_MASK + 0x7D1A)
#define HDMITX_DWC_CEC_TX_DATA11                        (DWC_OFFSET_MASK + 0x7D1B)
#define HDMITX_DWC_CEC_TX_DATA12                        (DWC_OFFSET_MASK + 0x7D1C)
#define HDMITX_DWC_CEC_TX_DATA13                        (DWC_OFFSET_MASK + 0x7D1D)
#define HDMITX_DWC_CEC_TX_DATA14                        (DWC_OFFSET_MASK + 0x7D1E)
#define HDMITX_DWC_CEC_TX_DATA15                        (DWC_OFFSET_MASK + 0x7D1F)
#define HDMITX_DWC_CEC_RX_DATA00                        (DWC_OFFSET_MASK + 0x7D20)
#define HDMITX_DWC_CEC_RX_DATA01                        (DWC_OFFSET_MASK + 0x7D21)
#define HDMITX_DWC_CEC_RX_DATA02                        (DWC_OFFSET_MASK + 0x7D22)
#define HDMITX_DWC_CEC_RX_DATA03                        (DWC_OFFSET_MASK + 0x7D23)
#define HDMITX_DWC_CEC_RX_DATA04                        (DWC_OFFSET_MASK + 0x7D24)
#define HDMITX_DWC_CEC_RX_DATA05                        (DWC_OFFSET_MASK + 0x7D25)
#define HDMITX_DWC_CEC_RX_DATA06                        (DWC_OFFSET_MASK + 0x7D26)
#define HDMITX_DWC_CEC_RX_DATA07                        (DWC_OFFSET_MASK + 0x7D27)
#define HDMITX_DWC_CEC_RX_DATA08                        (DWC_OFFSET_MASK + 0x7D28)
#define HDMITX_DWC_CEC_RX_DATA09                        (DWC_OFFSET_MASK + 0x7D29)
#define HDMITX_DWC_CEC_RX_DATA10                        (DWC_OFFSET_MASK + 0x7D2A)
#define HDMITX_DWC_CEC_RX_DATA11                        (DWC_OFFSET_MASK + 0x7D2B)
#define HDMITX_DWC_CEC_RX_DATA12                        (DWC_OFFSET_MASK + 0x7D2C)
#define HDMITX_DWC_CEC_RX_DATA13                        (DWC_OFFSET_MASK + 0x7D2D)
#define HDMITX_DWC_CEC_RX_DATA14                        (DWC_OFFSET_MASK + 0x7D2E)
#define HDMITX_DWC_CEC_RX_DATA15                        (DWC_OFFSET_MASK + 0x7D2F)
#define HDMITX_DWC_CEC_LOCK_BUF                         (DWC_OFFSET_MASK + 0x7D30)
#define HDMITX_DWC_CEC_WAKEUPCTRL                       (DWC_OFFSET_MASK + 0x7D31)
#define HDMITX_DWC_I2CM_SLAVE                           (DWC_OFFSET_MASK + 0x7E00)
#define HDMITX_DWC_I2CM_ADDRESS                         (DWC_OFFSET_MASK + 0x7E01)
#define HDMITX_DWC_I2CM_DATAO                           (DWC_OFFSET_MASK + 0x7E02)
#define HDMITX_DWC_I2CM_DATAI                           (DWC_OFFSET_MASK + 0x7E03)
#define HDMITX_DWC_I2CM_OPERATION                       (DWC_OFFSET_MASK + 0x7E04)
#define HDMITX_DWC_I2CM_INT                             (DWC_OFFSET_MASK + 0x7E05)
#define HDMITX_DWC_I2CM_CTLINT                          (DWC_OFFSET_MASK + 0x7E06)
#define HDMITX_DWC_I2CM_DIV                             (DWC_OFFSET_MASK + 0x7E07)
#define HDMITX_DWC_I2CM_SEGADDR                         (DWC_OFFSET_MASK + 0x7E08)
#define HDMITX_DWC_I2CM_SOFTRSTZ                        (DWC_OFFSET_MASK + 0x7E09)
#define HDMITX_DWC_I2CM_SEGPTR                          (DWC_OFFSET_MASK + 0x7E0A)
#define HDMITX_DWC_I2CM_SS_SCL_HCNT_1                   (DWC_OFFSET_MASK + 0x7E0B)
#define HDMITX_DWC_I2CM_SS_SCL_HCNT_0                   (DWC_OFFSET_MASK + 0x7E0C)
#define HDMITX_DWC_I2CM_SS_SCL_LCNT_1                   (DWC_OFFSET_MASK + 0x7E0D)
#define HDMITX_DWC_I2CM_SS_SCL_LCNT_0                   (DWC_OFFSET_MASK + 0x7E0E)
#define HDMITX_DWC_I2CM_FS_SCL_HCNT_1                   (DWC_OFFSET_MASK + 0x7E0F)
#define HDMITX_DWC_I2CM_FS_SCL_HCNT_0                   (DWC_OFFSET_MASK + 0x7E10)
#define HDMITX_DWC_I2CM_FS_SCL_LCNT_1                   (DWC_OFFSET_MASK + 0x7E11)
#define HDMITX_DWC_I2CM_FS_SCL_LCNT_0                   (DWC_OFFSET_MASK + 0x7E12)
#define HDMITX_DWC_I2CM_SDA_HOLD                        (DWC_OFFSET_MASK + 0x7E13)
#define HDMITX_DWC_I2CM_SCDC_UPDATE                     (DWC_OFFSET_MASK + 0x7E14)
#define HDMITX_DWC_I2CM_READ_BUFF0                      (DWC_OFFSET_MASK + 0x7E20)
#define HDMITX_DWC_I2CM_READ_BUFF1                      (DWC_OFFSET_MASK + 0x7E21)
#define HDMITX_DWC_I2CM_READ_BUFF2                      (DWC_OFFSET_MASK + 0x7E22)
#define HDMITX_DWC_I2CM_READ_BUFF3                      (DWC_OFFSET_MASK + 0x7E23)
#define HDMITX_DWC_I2CM_READ_BUFF4                      (DWC_OFFSET_MASK + 0x7E24)
#define HDMITX_DWC_I2CM_READ_BUFF5                      (DWC_OFFSET_MASK + 0x7E25)
#define HDMITX_DWC_I2CM_READ_BUFF6                      (DWC_OFFSET_MASK + 0x7E26)
#define HDMITX_DWC_I2CM_READ_BUFF7                      (DWC_OFFSET_MASK + 0x7E27)
#define HDMITX_DWC_I2CM_SCDC_UPDATE0                    (DWC_OFFSET_MASK + 0x7E30)
#define HDMITX_DWC_I2CM_SCDC_UPDATE1                    (DWC_OFFSET_MASK + 0x7E31)

/* viu */
#define VPU_VIU_ADDR_START                                  (0x1a00 << 2)
#define VPU_VIU_ADDR_END                                    (0x1aff << 2)
#define VPU_VIU_SW_RESET                                    (0x1a01 << 2)
#define VPU_VIU_MISC_CTRL0                                  (0x1a06 << 2)
#define VPU_D2D3_INTF_LENGTH                                (0x1a08 << 2)
#define VPU_D2D3_INTF_CTRL0                                 (0x1a09 << 2)
#define VPU_VIU_OSD1_CTRL_STAT                              (0x1a10 << 2)
#define VPU_VIU_OSD1_CTRL_STAT2                             (0x1a2d << 2)
#define VPU_VIU_OSD1_COLOR_ADDR                             (0x1a11 << 2)
#define VPU_VIU_OSD1_COLOR                                  (0x1a12 << 2)
#define VPU_VIU_OSD1_TCOLOR_AG0                             (0x1a17 << 2)
#define VPU_VIU_OSD1_TCOLOR_AG1                             (0x1a18 << 2)
#define VPU_VIU_OSD1_TCOLOR_AG2                             (0x1a19 << 2)
#define VPU_VIU_OSD1_TCOLOR_AG3                             (0x1a1a << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W0                            (0x1a1b << 2)
#define VPU_VIU_OSD1_BLK1_CFG_W0                            (0x1a1f << 2)
#define VPU_VIU_OSD1_BLK2_CFG_W0                            (0x1a23 << 2)
#define VPU_VIU_OSD1_BLK3_CFG_W0                            (0x1a27 << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W1                            (0x1a1c << 2)
#define VPU_VIU_OSD1_BLK1_CFG_W1                            (0x1a20 << 2)
#define VPU_VIU_OSD1_BLK2_CFG_W1                            (0x1a24 << 2)
#define VPU_VIU_OSD1_BLK3_CFG_W1                            (0x1a28 << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W2                            (0x1a1d << 2)
#define VPU_VIU_OSD1_BLK1_CFG_W2                            (0x1a21 << 2)
#define VPU_VIU_OSD1_BLK2_CFG_W2                            (0x1a25 << 2)
#define VPU_VIU_OSD1_BLK3_CFG_W2                            (0x1a29 << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W3                            (0x1a1e << 2)
#define VPU_VIU_OSD1_BLK1_CFG_W3                            (0x1a22 << 2)
#define VPU_VIU_OSD1_BLK2_CFG_W3                            (0x1a26 << 2)
#define VPU_VIU_OSD1_BLK3_CFG_W3                            (0x1a2a << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W4                            (0x1a13 << 2)
#define VPU_VIU_OSD1_BLK1_CFG_W4                            (0x1a14 << 2)
#define VPU_VIU_OSD1_BLK2_CFG_W4                            (0x1a15 << 2)
#define VPU_VIU_OSD1_BLK3_CFG_W4                            (0x1a16 << 2)
#define VPU_VIU_OSD1_FIFO_CTRL_STAT                         (0x1a2b << 2)
#define VPU_VIU_OSD1_TEST_RDDATA                            (0x1a2c << 2)
#define VPU_VIU_OSD1_PROT_CTRL                              (0x1a2e << 2)
#define VPU_VIU_OSD2_CTRL_STAT                              (0x1a30 << 2)
#define VPU_VIU_OSD2_CTRL_STAT2                             (0x1a4d << 2)
#define VPU_VIU_OSD2_COLOR_ADDR                             (0x1a31 << 2)
#define VPU_VIU_OSD2_COLOR                                  (0x1a32 << 2)
#define VPU_VIU_OSD2_HL1_H_START_END                        (0x1a33 << 2)
#define VPU_VIU_OSD2_HL1_V_START_END                        (0x1a34 << 2)
#define VPU_VIU_OSD2_HL2_H_START_END                        (0x1a35 << 2)
#define VPU_VIU_OSD2_HL2_V_START_END                        (0x1a36 << 2)
#define VPU_VIU_OSD2_TCOLOR_AG0                             (0x1a37 << 2)
#define VPU_VIU_OSD2_TCOLOR_AG1                             (0x1a38 << 2)
#define VPU_VIU_OSD2_TCOLOR_AG2                             (0x1a39 << 2)
#define VPU_VIU_OSD2_TCOLOR_AG3                             (0x1a3a << 2)
#define VPU_VIU_OSD2_BLK0_CFG_W0                            (0x1a3b << 2)
#define VPU_VIU_OSD2_BLK1_CFG_W0                            (0x1a3f << 2)
#define VPU_VIU_OSD2_BLK2_CFG_W0                            (0x1a43 << 2)
#define VPU_VIU_OSD2_BLK3_CFG_W0                            (0x1a47 << 2)
#define VPU_VIU_OSD2_BLK0_CFG_W1                            (0x1a3c << 2)
#define VPU_VIU_OSD2_BLK1_CFG_W1                            (0x1a40 << 2)
#define VPU_VIU_OSD2_BLK2_CFG_W1                            (0x1a44 << 2)
#define VPU_VIU_OSD2_BLK3_CFG_W1                            (0x1a48 << 2)
#define VPU_VIU_OSD2_BLK0_CFG_W2                            (0x1a3d << 2)
#define VPU_VIU_OSD2_BLK1_CFG_W2                            (0x1a41 << 2)
#define VPU_VIU_OSD2_BLK2_CFG_W2                            (0x1a45 << 2)
#define VPU_VIU_OSD2_BLK3_CFG_W2                            (0x1a49 << 2)
#define VPU_VIU_OSD2_BLK0_CFG_W3                            (0x1a3e << 2)
#define VPU_VIU_OSD2_BLK1_CFG_W3                            (0x1a42 << 2)
#define VPU_VIU_OSD2_BLK2_CFG_W3                            (0x1a46 << 2)
#define VPU_VIU_OSD2_BLK3_CFG_W3                            (0x1a4a << 2)
#define VPU_VIU_OSD2_BLK0_CFG_W4                            (0x1a64 << 2)
#define VPU_VIU_OSD2_BLK1_CFG_W4                            (0x1a65 << 2)
#define VPU_VIU_OSD2_BLK2_CFG_W4                            (0x1a66 << 2)
#define VPU_VIU_OSD2_BLK3_CFG_W4                            (0x1a67 << 2)
#define VPU_VIU_OSD2_FIFO_CTRL_STAT                         (0x1a4b << 2)
#define VPU_VIU_OSD2_TEST_RDDATA                            (0x1a4c << 2)
#define VPU_VIU_OSD2_PROT_CTRL                              (0x1a4e << 2)

#define VPU_VPP_OSD_SCO_H_START_END                         (0x1dca << 2)
#define VPU_VPP_OSD_SCO_V_START_END                         (0x1dcb << 2)
#define VPU_VPP_POSTBLEND_H_SIZE                            (0x1d21 << 2)
#define VPU_VPP_OSD_SCI_WH_M1                               (0x1dc9 << 2)

#define VPU_ENCP_VIDEO_EN                                   (0x1b80 << 2)
#define VPU_ENCI_VIDEO_EN                                   (0x1b57 << 2)
#define VPU_ENCP_VIDEO_FILT_CTRL                            (0x1bb8 << 2)
#define VPU_VENC_DVI_SETTING                                (0x1b62 << 2)
#define VPU_ENCP_VIDEO_MODE                                 (0x1b8d << 2)
#define VPU_ENCP_VIDEO_MODE_ADV                             (0x1b8e << 2)
#define VPU_VENC_VIDEO_TST_Y                                (0x1b72 << 2)
#define VPU_VENC_VIDEO_TST_CB                               (0x1b73 << 2)
#define VPU_VENC_VIDEO_TST_CR                               (0x1b74 << 2)
#define VPU_VENC_VIDEO_TST_CLRBAR_STRT                      (0x1b75 << 2)
#define VPU_VENC_VIDEO_TST_CLRBAR_WIDTH                     (0x1b76 << 2)
#define VPU_ENCP_VIDEO_YFP1_HTIME                           (0x1b94 << 2)
#define VPU_ENCP_VIDEO_YFP2_HTIME                           (0x1b95 << 2)
#define VPU_ENCP_VIDEO_MAX_PXCNT                            (0x1b97 << 2)
#define VPU_ENCP_VIDEO_HSPULS_BEGIN                         (0x1b98 << 2)
#define VPU_ENCP_VIDEO_HSPULS_END                           (0x1b99 << 2)
#define VPU_ENCP_VIDEO_HSPULS_SWITCH                        (0x1b9a << 2)
#define VPU_ENCP_VIDEO_VSPULS_BEGIN                         (0x1b9b << 2)
#define VPU_ENCP_VIDEO_VSPULS_END                           (0x1b9c << 2)
#define VPU_ENCP_VIDEO_VSPULS_BLINE                         (0x1b9d << 2)
#define VPU_ENCP_VIDEO_VSPULS_ELINE                         (0x1b9e << 2)
#define VPU_ENCP_VIDEO_HAVON_END                            (0x1ba3 << 2)
#define VPU_ENCP_VIDEO_HAVON_BEGIN                          (0x1ba4 << 2)
#define VPU_ENCP_VIDEO_VAVON_ELINE                          (0x1baf << 2)
#define VPU_ENCP_VIDEO_VAVON_BLINE                          (0x1ba6 << 2)
#define VPU_ENCP_VIDEO_HSO_BEGIN                            (0x1ba7 << 2)
#define VPU_ENCP_VIDEO_HSO_END                              (0x1ba8 << 2)
#define VPU_ENCP_VIDEO_VSO_BEGIN                            (0x1ba9 << 2)
#define VPU_ENCP_VIDEO_VSO_END                              (0x1baa << 2)
#define VPU_ENCP_VIDEO_VSO_BLINE                            (0x1bab << 2)
#define VPU_ENCP_VIDEO_VSO_ELINE                            (0x1bac << 2)
#define VPU_ENCP_VIDEO_SYNC_WAVE_CURVE                      (0x1bad << 2)
#define VPU_ENCP_VIDEO_MAX_LNCNT                            (0x1bae << 2)
#define VPU_ENCP_VIDEO_EN                                   (0x1b80 << 2)
#define VPU_ENCP_VIDEO_SYNC_MODE                            (0x1b81 << 2)
#define VPU_ENCP_MACV_EN                                    (0x1b82 << 2)
#define VPU_ENCP_VIDEO_Y_SCL                                (0x1b83 << 2)
#define VPU_ENCP_VIDEO_PB_SCL                               (0x1b84 << 2)
#define VPU_ENCP_VIDEO_PR_SCL                               (0x1b85 << 2)
#define VPU_ENCP_VIDEO_SYNC_SCL                             (0x1b86 << 2)
#define VPU_ENCP_VIDEO_MACV_SCL                             (0x1b87 << 2)
#define VPU_ENCP_VIDEO_Y_OFFST                              (0x1b88 << 2)
#define VPU_ENCP_VIDEO_PB_OFFST                             (0x1b89 << 2)
#define VPU_ENCP_VIDEO_PR_OFFST                             (0x1b8a << 2)
#define VPU_ENCP_VIDEO_SYNC_OFFST                           (0x1b8b << 2)
#define VPU_ENCP_VIDEO_MACV_OFFST                           (0x1b8c << 2)
#define VPU_ENCP_VIDEO_SY_VAL                               (0x1bb0 << 2)
#define VPU_ENCP_VIDEO_SY2_VAL                              (0x1bb1 << 2)
#define VPU_ENCP_VIDEO_BLANKY_VAL                           (0x1bb2 << 2)
#define VPU_ENCP_VIDEO_BLANKPB_VAL                          (0x1bb3 << 2)
#define VPU_ENCP_VIDEO_BLANKPR_VAL                          (0x1bb4 << 2)


#define VPU_VPU_VIU_VENC_MUX_CTRL                           (0x271a << 2)
 #define VIU_VENC_MUX_CTRL_VIU2(x)                  (x << 2)
 #define VIU_VENC_MUX_CTRL_VIU1(x)                  (x << 0)

#define VPU_VENC_VIDEO_PROG_MODE                            (0x1b68 << 2)
#define VPU_ENCP_DE_H_BEGIN                                 (0x1c3a << 2)
#define VPU_ENCP_DE_H_END                                   (0x1c3b << 2)
#define VPU_ENCP_DE_V_BEGIN_EVEN                            (0x1c3c << 2)
#define VPU_ENCP_DE_V_END_EVEN                              (0x1c3d << 2)
#define VPU_ENCP_DVI_HSO_BEGIN                              (0x1c30 << 2)
#define VPU_ENCP_DVI_HSO_END                                (0x1c31 << 2)
#define VPU_ENCP_DVI_VSO_BLINE_EVN                          (0x1c32 << 2)
#define VPU_ENCP_DVI_VSO_ELINE_EVN                          (0x1c34 << 2)
#define VPU_ENCP_DVI_VSO_BEGIN_EVN                          (0x1c36 << 2)
#define VPU_ENCP_DVI_VSO_END_EVN                            (0x1c38 << 2)
#define VPU_HDMI_SETTING                                    (0x271b << 2)
#define VPU_HDMI_FMT_CTRL                                   (0x2743 << 2)
#define VPU_HDMI_DITH_CNTL                                  (0x27fc << 2)
#define VPU_VENC_VIDEO_TST_EN                               (0x1b70 << 2)
#define VPU_VENC_VIDEO_TST_MDSEL                            (0x1b71 << 2)

#define VPU_VPP_DUMMY_DATA                                  (0x1d00 << 2)
#define VPU_VPP_LINE_IN_LENGTH                              (0x1d01 << 2)
#define VPU_VPP_PIC_IN_HEIGHT                               (0x1d02 << 2)
#define VPU_VPP_SCALE_COEF_IDX                              (0x1d03 << 2)
#define VPU_VPP_SCALE_COEF                                  (0x1d04 << 2)
#define VPU_VPP_VSC_REGION12_STARTP                         (0x1d05 << 2)
#define VPU_VPP_VSC_REGION34_STARTP                         (0x1d06 << 2)
#define VPU_VPP_VSC_REGION4_ENDP                            (0x1d07 << 2)
#define VPU_VPP_VSC_START_PHASE_STEP                        (0x1d08 << 2)
#define VPU_VPP_VSC_REGION0_PHASE_SLOPE                     (0x1d09 << 2)
#define VPU_VPP_VSC_REGION1_PHASE_SLOPE                         (0x1d0a << 2)
#define VPU_VPP_VSC_REGION3_PHASE_SLOPE                         (0x1d0b << 2)
#define VPU_VPP_VSC_REGION4_PHASE_SLOPE                         (0x1d0c << 2)
#define VPU_VPP_VSC_PHASE_CTRL                      (0x1d0d << 2)
#define VPU_VPP_VSC_INI_PHASE                       (0x1d0e << 2)
#define VPU_VPP_HSC_REGION12_STARTP                         (0x1d10 << 2)
#define VPU_VPP_HSC_REGION34_STARTP                         (0x1d11 << 2)
#define VPU_VPP_HSC_REGION4_ENDP                        (0x1d12 << 2)
#define VPU_VPP_HSC_START_PHASE_STEP                        (0x1d13 << 2)
#define VPU_VPP_HSC_REGION0_PHASE_SLOPE                         (0x1d14 << 2)
#define VPU_VPP_HSC_REGION1_PHASE_SLOPE                         (0x1d15 << 2)
#define VPU_VPP_HSC_REGION3_PHASE_SLOPE                         (0x1d16 << 2)
#define VPU_VPP_HSC_REGION4_PHASE_SLOPE                         (0x1d17 << 2)
#define VPU_VPP_HSC_PHASE_CTRL                      (0x1d18 << 2)
#define VPU_VPP_SC_MISC                         (0x1d19 << 2)
#define VPU_VPP_PREBLEND_VD1_H_START_END                        (0x1d1a << 2)
#define VPU_VPP_PREBLEND_VD1_V_START_END                        (0x1d1b << 2)
#define VPU_VPP_POSTBLEND_VD1_H_START_END                       (0x1d1c << 2)
#define VPU_VPP_POSTBLEND_VD1_V_START_END                       (0x1d1d << 2)
#define VPU_VPP_BLEND_VD2_H_START_END                       (0x1d1e << 2)
#define VPU_VPP_BLEND_VD2_V_START_END                       (0x1d1f << 2)
#define VPU_VPP_PREBLEND_H_SIZE                         (0x1d20 << 2)
#define VPU_VPP_POSTBLEND_H_SIZE                        (0x1d21 << 2)
#define VPU_VPP_HOLD_LINES                      (0x1d22 << 2)
#define VPU_VPP_BLEND_ONECOLOR_CTRL                         (0x1d23 << 2)
#define VPU_VPP_PREBLEND_CURRENT_XY                         (0x1d24 << 2)
#define VPU_VPP_POSTBLEND_CURRENT_XY                        (0x1d25 << 2)
#define VPU_VPP_MISC                        (0x1d26 << 2)
#define VPU_VPP_OFIFO_SIZE                      (0x1d27 << 2)
#define VPU_VPP_FIFO_STATUS                         (0x1d28 << 2)
#define VPU_VPP_SMOKE_CTRL                      (0x1d29 << 2)
#define VPU_VPP_SMOKE1_VAL                      (0x1d2a << 2)
#define VPU_VPP_SMOKE2_VAL                      (0x1d2b << 2)
#define VPU_VPP_SMOKE3_VAL                      (0x1d2c << 2)
#define VPU_VPP_SMOKE1_H_START_END                      (0x1d2d << 2)
#define VPU_VPP_SMOKE1_V_START_END                      (0x1d2e << 2)
#define VPU_VPP_SMOKE2_H_START_END                      (0x1d2f << 2)
#define VPU_VPP_SMOKE2_V_START_END                      (0x1d30 << 2)
#define VPU_VPP_SMOKE3_H_START_END                      (0x1d31 << 2)
#define VPU_VPP_SMOKE3_V_START_END                      (0x1d32 << 2)
#define VPU_VPP_SCO_FIFO_CTRL                       (0x1d33 << 2)
#define VPU_VPP_HSC_PHASE_CTRL1                         (0x1d34 << 2)
#define VPU_VPP_HSC_INI_PAT_CTRL                        (0x1d35 << 2)
#define VPU_VPP_VADJ_CTRL                       (0x1d40 << 2)
#define VPU_VPP_VADJ1_Y                         (0x1d41 << 2)
#define VPU_VPP_VADJ1_MA_MB                         (0x1d42 << 2)
#define VPU_VPP_VADJ1_MC_MD                         (0x1d43 << 2)
#define VPU_VPP_VADJ2_Y                         (0x1d44 << 2)
#define VPU_VPP_VADJ2_MA_MB                         (0x1d45 << 2)
#define VPU_VPP_VADJ2_MC_MD                         (0x1d46 << 2)
#define VPU_VPP_HSHARP_CTRL                         (0x1d50 << 2)
#define VPU_VPP_HSHARP_LUMA_THRESH01                        (0x1d51 << 2)
#define VPU_VPP_HSHARP_LUMA_THRESH23                        (0x1d52 << 2)
#define VPU_VPP_HSHARP_CHROMA_THRESH01                      (0x1d53 << 2)
#define VPU_VPP_HSHARP_CHROMA_THRESH23                      (0x1d54 << 2)
#define VPU_VPP_HSHARP_LUMA_GAIN                        (0x1d55 << 2)
#define VPU_VPP_HSHARP_CHROMA_GAIN                      (0x1d56 << 2)
#define VPU_VPP_MATRIX_PROBE_COLOR                      (0x1d5c << 2)
#define VPU_VPP_MATRIX_HL_COLOR                         (0x1d5d << 2)
#define VPU_VPP_MATRIX_PROBE_POS                        (0x1d5e << 2)
#define VPU_VPP_MATRIX_CTRL                         (0x1d5f << 2)
#define VPU_VPP_MATRIX_COEF00_01                        (0x1d60 << 2)
#define VPU_VPP_MATRIX_COEF02_10                        (0x1d61 << 2)
#define VPU_VPP_MATRIX_COEF11_12                        (0x1d62 << 2)
#define VPU_VPP_MATRIX_COEF20_21                        (0x1d63 << 2)
#define VPU_VPP_MATRIX_COEF22                       (0x1d64 << 2)
#define VPU_VPP_MATRIX_OFFSET0_1                        (0x1d65 << 2)
#define VPU_VPP_MATRIX_OFFSET2                      (0x1d66 << 2)
#define VPU_VPP_MATRIX_PRE_OFFSET0_1                        (0x1d67 << 2)
#define VPU_VPP_MATRIX_PRE_OFFSET2                      (0x1d68 << 2)
#define VPU_VPP_DUMMY_DATA1                         (0x1d69 << 2)
#define VPU_VPP_GAINOFF_CTRL0                       (0x1d6a << 2)
#define VPU_VPP_GAINOFF_CTRL1                       (0x1d6b << 2)
#define VPU_VPP_GAINOFF_CTRL2                       (0x1d6c << 2)
#define VPU_VPP_GAINOFF_CTRL3                       (0x1d6d << 2)
#define VPU_VPP_GAINOFF_CTRL4                       (0x1d6e << 2)
#define VPU_VPP_CHROMA_ADDR_PORT                        (0x1d70 << 2)
#define VPU_VPP_CHROMA_DATA_PORT                        (0x1d71 << 2)
#define VPU_VPP_GCLK_CTRL0                      (0x1d72 << 2)
#define VPU_VPP_GCLK_CTRL1                      (0x1d73 << 2)
#define VPU_VPP_SC_GCLK_CTRL                        (0x1d74 << 2)
#define VPU_VPP_MISC1                       (0x1d76 << 2)
#define VPU_VPP_BLACKEXT_CTRL                       (0x1d80 << 2)
#define VPU_VPP_DNLP_CTRL_00                        (0x1d81 << 2)
#define VPU_VPP_DNLP_CTRL_01                        (0x1d82 << 2)
#define VPU_VPP_DNLP_CTRL_02                        (0x1d83 << 2)
#define VPU_VPP_DNLP_CTRL_03                        (0x1d84 << 2)
#define VPU_VPP_DNLP_CTRL_04                        (0x1d85 << 2)
#define VPU_VPP_DNLP_CTRL_05                        (0x1d86 << 2)
#define VPU_VPP_DNLP_CTRL_06                        (0x1d87 << 2)
#define VPU_VPP_DNLP_CTRL_07                        (0x1d88 << 2)
#define VPU_VPP_DNLP_CTRL_08                        (0x1d89 << 2)
#define VPU_VPP_DNLP_CTRL_09                        (0x1d8a << 2)
#define VPU_VPP_DNLP_CTRL_10                        (0x1d8b << 2)
#define VPU_VPP_DNLP_CTRL_11                        (0x1d8c << 2)
#define VPU_VPP_DNLP_CTRL_12                        (0x1d8d << 2)
#define VPU_VPP_DNLP_CTRL_13                        (0x1d8e << 2)
#define VPU_VPP_DNLP_CTRL_14                        (0x1d8f << 2)
#define VPU_VPP_DNLP_CTRL_15                        (0x1d90 << 2)
#define VPU_VPP_PEAKING_HGAIN                       (0x1d91 << 2)
#define VPU_VPP_PEAKING_VGAIN                       (0x1d92 << 2)
#define VPU_VPP_PEAKING_NLP_1                       (0x1d93 << 2)
#define VPU_VPP_PEAKING_NLP_2                       (0x1d94 << 2)
#define VPU_VPP_PEAKING_NLP_3                       (0x1d95 << 2)
#define VPU_VPP_PEAKING_NLP_4                       (0x1d96 << 2)
#define VPU_VPP_PEAKING_NLP_5                       (0x1d97 << 2)
#define VPU_VPP_SHARP_LIMIT                         (0x1d98 << 2)
#define VPU_VPP_VLTI_CTRL                       (0x1d99 << 2)
#define VPU_VPP_HLTI_CTRL                       (0x1d9a << 2)
#define VPU_VPP_CTI_CTRL                        (0x1d9b << 2)
#define VPU_VPP_BLUE_STRETCH_1                      (0x1d9c << 2)
#define VPU_VPP_BLUE_STRETCH_2                      (0x1d9d << 2)
#define VPU_VPP_BLUE_STRETCH_3                      (0x1d9e << 2)
#define VPU_VPP_CCORING_CTRL                        (0x1da0 << 2)
#define VPU_VPP_VE_ENABLE_CTRL                      (0x1da1 << 2)
#define VPU_VPP_VE_DEMO_LEFT_TOP_SCREEN_WIDTH                       (0x1da2 << 2)
#define VPU_VPP_VE_DEMO_CENTER_BAR                      (0x1da3 << 2)
#define VPU_VPP_VE_H_V_SIZE                         (0x1da4 << 2)
#define VPU_VPP_VDO_MEAS_CTRL                       (0x1da8 << 2)
#define VPU_VPP_VDO_MEAS_VS_COUNT_HI                        (0x1da9 << 2)
#define VPU_VPP_VDO_MEAS_VS_COUNT_LO                        (0x1daa << 2)
#define VPU_VPP_INPUT_CTRL                      (0x1dab << 2)
#define VPU_VPP_CTI_CTRL2                       (0x1dac << 2)
#define VPU_VPP_PEAKING_SAT_THD1                        (0x1dad << 2)
#define VPU_VPP_PEAKING_SAT_THD2                        (0x1dae << 2)
#define VPU_VPP_PEAKING_SAT_THD3                        (0x1daf << 2)
#define VPU_VPP_PEAKING_SAT_THD4                        (0x1db0 << 2)
#define VPU_VPP_PEAKING_SAT_THD5                        (0x1db1 << 2)
#define VPU_VPP_PEAKING_SAT_THD6                        (0x1db2 << 2)
#define VPU_VPP_PEAKING_SAT_THD7                        (0x1db3 << 2)
#define VPU_VPP_PEAKING_SAT_THD8                        (0x1db4 << 2)
#define VPU_VPP_PEAKING_SAT_THD9                        (0x1db5 << 2)
#define VPU_VPP_PEAKING_GAIN_ADD1                       (0x1db6 << 2)
#define VPU_VPP_PEAKING_GAIN_ADD2                       (0x1db7 << 2)
#define VPU_VPP_PEAKING_DNLP                        (0x1db8 << 2)
#define VPU_VPP_SHARP_DEMO_WIN_CTRL1                        (0x1db9 << 2)
#define VPU_VPP_SHARP_DEMO_WIN_CTRL2                        (0x1dba << 2)
#define VPU_VPP_FRONT_HLTI_CTRL                         (0x1dbb << 2)
#define VPU_VPP_FRONT_CTI_CTRL                      (0x1dbc << 2)
#define VPU_VPP_FRONT_CTI_CTRL2                         (0x1dbd << 2)
#define VPU_VPP_OSD_VSC_PHASE_STEP                      (0x1dc0 << 2)
#define VPU_VPP_OSD_VSC_INI_PHASE                       (0x1dc1 << 2)
#define VPU_VPP_OSD_VSC_CTRL0                       (0x1dc2 << 2)
#define VPU_VPP_OSD_HSC_PHASE_STEP                      (0x1dc3 << 2)
#define VPU_VPP_OSD_HSC_INI_PHASE                       (0x1dc4 << 2)
#define VPU_VPP_OSD_HSC_CTRL0                       (0x1dc5 << 2)
#define VPU_VPP_OSD_HSC_INI_PAT_CTRL                        (0x1dc6 << 2)
#define VPU_VPP_OSD_SC_DUMMY_DATA                       (0x1dc7 << 2)
#define VPU_VPP_OSD_SC_CTRL0                        (0x1dc8 << 2)
#define VPU_VPP_OSD_SCI_WH_M1                       (0x1dc9 << 2)
#define VPU_VPP_OSD_SCO_H_START_END                         (0x1dca << 2)
#define VPU_VPP_OSD_SCO_V_START_END                         (0x1dcb << 2)
#define VPU_VPP_OSD_SCALE_COEF_IDX                      (0x1dcc << 2)
#define VPU_VPP_OSD_SCALE_COEF                      (0x1dcd << 2)
#define VPU_VPP_INT_LINE_NUM                        (0x1dce << 2)

struct reg_val_pair {
    uint32_t    reg;
    uint32_t    val;
};


static const struct reg_val_pair ENC_LUT_1080p[] = {
    { VPU_ENCP_VIDEO_EN, 0},
    { VPU_ENCI_VIDEO_EN, 0},
    { VPU_ENCP_VIDEO_FILT_CTRL, 0x1052},
    { VPU_VENC_DVI_SETTING, 0x0001},
    { VPU_ENCP_VIDEO_MODE, 0x4040},
    { VPU_ENCP_VIDEO_MODE_ADV, 0x0018},
    { VPU_ENCP_VIDEO_YFP1_HTIME, 140},
    { VPU_ENCP_VIDEO_YFP2_HTIME, 2060},
    { VPU_ENCP_VIDEO_MAX_PXCNT, 2199},
    { VPU_ENCP_VIDEO_HSPULS_BEGIN, 2156},   // HBP + HACT + HFP
    { VPU_ENCP_VIDEO_HSPULS_END, 44},       // HSYNC
    { VPU_ENCP_VIDEO_HSPULS_SWITCH, 44},
    { VPU_ENCP_VIDEO_VSPULS_BEGIN, 140},
    { VPU_ENCP_VIDEO_VSPULS_END, 2059},
    { VPU_ENCP_VIDEO_VSPULS_BLINE, 0},
    { VPU_ENCP_VIDEO_VSPULS_ELINE, 4},
    { VPU_ENCP_VIDEO_HAVON_BEGIN, 148},
    { VPU_ENCP_VIDEO_HAVON_END, 2067},
    { VPU_ENCP_VIDEO_VAVON_BLINE, 41},
    { VPU_ENCP_VIDEO_VAVON_ELINE, 1120},
    { VPU_ENCP_VIDEO_HSO_BEGIN, 44},
    { VPU_ENCP_VIDEO_HSO_END, 2156},
    { VPU_ENCP_VIDEO_VSO_BEGIN, 2100},
    { VPU_ENCP_VIDEO_VSO_END, 2164},
    { VPU_ENCP_VIDEO_VSO_BLINE, 0},
    { VPU_ENCP_VIDEO_VSO_ELINE, 5},
    { VPU_ENCP_VIDEO_MAX_LNCNT, 1124},
    { VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    { VPU_VENC_VIDEO_PROG_MODE, 0x100},
    { VPU_ENCI_VIDEO_EN, 0},
    { VPU_ENCP_VIDEO_EN, 1},
    { 0xFFFFFFFF, 0},
};

static const struct reg_val_pair ENC_LUT_640x480p[] = {
    {VPU_ENCP_VIDEO_EN, 0,},
    {VPU_ENCI_VIDEO_EN, 0,},
    {VPU_ENCP_VIDEO_MODE, 0x4040,},
    {VPU_ENCP_VIDEO_MODE_ADV, 0x18,},
    {VPU_ENCP_VIDEO_MAX_PXCNT, 0x31F,},
    {VPU_ENCP_VIDEO_MAX_LNCNT, 0x20C,},
    {VPU_ENCP_VIDEO_HAVON_BEGIN, 0x90,},
    {VPU_ENCP_VIDEO_HAVON_END, 0x30F,},
    {VPU_ENCP_VIDEO_VAVON_BLINE, 0x23,},
    {VPU_ENCP_VIDEO_VAVON_ELINE, 0x202,},
    {VPU_ENCP_VIDEO_HSO_BEGIN, 0x0,},
    {VPU_ENCP_VIDEO_HSO_END, 0x60,},
    {VPU_ENCP_VIDEO_VSO_BEGIN, 0x1E,},
    {VPU_ENCP_VIDEO_VSO_END, 0x32,},
    {VPU_ENCP_VIDEO_VSO_BLINE, 0x0,},
    {VPU_ENCP_VIDEO_VSO_ELINE, 0x2,},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    {VPU_ENCP_VIDEO_EN, 1,},
    {VPU_ENCI_VIDEO_EN, 0,},
    { 0xFFFFFFFF, 0},
};

static const struct reg_val_pair ENC_LUT_720p[] = {
    {VPU_ENCP_VIDEO_EN, 0},
    {VPU_ENCI_VIDEO_EN, 0},
    {VPU_VENC_DVI_SETTING, 0x2029},
    {VPU_ENCP_VIDEO_MODE, 0x4040},
    {VPU_ENCP_VIDEO_MODE_ADV, 0x0019},
    {VPU_ENCP_VIDEO_YFP1_HTIME, 648},
    {VPU_ENCP_VIDEO_YFP2_HTIME, 3207},
    {VPU_ENCP_VIDEO_MAX_PXCNT, 3299},
    {VPU_ENCP_VIDEO_HSPULS_BEGIN, 80},
    {VPU_ENCP_VIDEO_HSPULS_END, 240},
    {VPU_ENCP_VIDEO_HSPULS_SWITCH, 80},
    {VPU_ENCP_VIDEO_VSPULS_BEGIN, 688},
    {VPU_ENCP_VIDEO_VSPULS_END, 3248},
    {VPU_ENCP_VIDEO_VSPULS_BLINE, 4},
    {VPU_ENCP_VIDEO_VSPULS_ELINE, 8},
    {VPU_ENCP_VIDEO_HAVON_BEGIN, 648},
    {VPU_ENCP_VIDEO_HAVON_END, 3207},
    {VPU_ENCP_VIDEO_VAVON_BLINE, 29},
    {VPU_ENCP_VIDEO_VAVON_ELINE, 748},
    {VPU_ENCP_VIDEO_HSO_BEGIN, 256},
    {VPU_ENCP_VIDEO_HSO_END, 168},
    {VPU_ENCP_VIDEO_VSO_BEGIN, 168},
    {VPU_ENCP_VIDEO_VSO_END, 256},
    {VPU_ENCP_VIDEO_VSO_BLINE, 0},
    {VPU_ENCP_VIDEO_VSO_ELINE, 5},
    {VPU_ENCP_VIDEO_MAX_LNCNT, 749},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    {VPU_ENCP_VIDEO_EN, 1},
    {VPU_ENCI_VIDEO_EN, 0},
    { 0xFFFFFFFF, 0},
};

static const struct reg_val_pair ENC_LUT_800p[] = {
    {VPU_ENCP_VIDEO_EN, 0,},
    {VPU_ENCI_VIDEO_EN, 0,},
    {VPU_ENCP_VIDEO_MODE, 0x4040,},
    {VPU_ENCP_VIDEO_MODE_ADV, 0x18,},
    {VPU_ENCP_VIDEO_MAX_PXCNT, 0x59F,},
    {VPU_ENCP_VIDEO_MAX_LNCNT, 0x336,},
    {VPU_ENCP_VIDEO_HAVON_BEGIN, 0x70,},
    {VPU_ENCP_VIDEO_HAVON_END, 0x56F,},
    {VPU_ENCP_VIDEO_VAVON_BLINE, 0x14,},
    {VPU_ENCP_VIDEO_VAVON_ELINE, 0x333,},
    {VPU_ENCP_VIDEO_HSO_BEGIN, 0x0,},
    {VPU_ENCP_VIDEO_HSO_END, 0x20,},
    {VPU_ENCP_VIDEO_VSO_BEGIN, 0x1E,},
    {VPU_ENCP_VIDEO_VSO_END, 0x32,},
    {VPU_ENCP_VIDEO_VSO_BLINE, 0x0,},
    {VPU_ENCP_VIDEO_VSO_ELINE, 0x6,},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_EN, 1},
    { 0xFFFFFFFF, 0},
};

static const struct reg_val_pair ENC_LUT_480p[] = {
    {VPU_ENCP_VIDEO_EN, 0},
    {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_FILT_CTRL, 0x2052},
    {VPU_VENC_DVI_SETTING, 0x21},
    {VPU_ENCP_VIDEO_MODE, 0x4000},
    {VPU_ENCP_VIDEO_MODE_ADV, 9},
    {VPU_ENCP_VIDEO_YFP1_HTIME, 244},
    {VPU_ENCP_VIDEO_YFP2_HTIME, 1630},
    {VPU_ENCP_VIDEO_MAX_PXCNT, 1715},
    {VPU_ENCP_VIDEO_MAX_LNCNT, 524},
    {VPU_ENCP_VIDEO_HSPULS_BEGIN, 0x22},
    {VPU_ENCP_VIDEO_HSPULS_END, 0xa0},
    {VPU_ENCP_VIDEO_HSPULS_SWITCH, 88},
    {VPU_ENCP_VIDEO_VSPULS_BEGIN, 0},
    {VPU_ENCP_VIDEO_VSPULS_END, 1589},
    {VPU_ENCP_VIDEO_VSPULS_BLINE, 0},
    {VPU_ENCP_VIDEO_VSPULS_ELINE, 5},
    {VPU_ENCP_VIDEO_HAVON_BEGIN, 249},
    {VPU_ENCP_VIDEO_HAVON_END, 1689},
    {VPU_ENCP_VIDEO_VAVON_BLINE, 42},
    {VPU_ENCP_VIDEO_VAVON_ELINE, 521},
    {VPU_ENCP_VIDEO_SYNC_MODE, 0x07},
    {VPU_VENC_VIDEO_PROG_MODE, 0x0},
    {VPU_ENCP_VIDEO_HSO_BEGIN, 0x3},
    {VPU_ENCP_VIDEO_HSO_END, 0x5},
    {VPU_ENCP_VIDEO_VSO_BEGIN, 0x3},
    {VPU_ENCP_VIDEO_VSO_END, 0x5},
    {VPU_ENCP_VIDEO_VSO_BLINE, 0},
    {VPU_ENCP_VIDEO_SY_VAL, 8},
    {VPU_ENCP_VIDEO_SY2_VAL, 0x1d8},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_EN, 1},
    { 0xFFFFFFFF, 0},
};

static const struct reg_val_pair ENC_LUT_1280x1024p60hz[] = {
    {VPU_ENCP_VIDEO_EN, 0,},
    {VPU_ENCI_VIDEO_EN, 0,},
    {VPU_ENCP_VIDEO_MODE, 0x4040,},
    {VPU_ENCP_VIDEO_MODE_ADV, 0x18,},
    {VPU_ENCP_VIDEO_MAX_PXCNT, 0x697,},
    {VPU_ENCP_VIDEO_MAX_LNCNT, 0x429,},
    {VPU_ENCP_VIDEO_HAVON_BEGIN, 0x168,},
    {VPU_ENCP_VIDEO_HAVON_END, 0x667,},
    {VPU_ENCP_VIDEO_VAVON_BLINE, 0x29,},
    {VPU_ENCP_VIDEO_VAVON_ELINE, 0x428,},
    {VPU_ENCP_VIDEO_HSO_BEGIN, 0x0,},
    {VPU_ENCP_VIDEO_HSO_END, 0x70,},
    {VPU_ENCP_VIDEO_VSO_BEGIN, 0x1E,},
    {VPU_ENCP_VIDEO_VSO_END, 0x32,},
    {VPU_ENCP_VIDEO_VSO_BLINE, 0x0,},
    {VPU_ENCP_VIDEO_VSO_ELINE, 0x3,},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA},
    {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_EN, 1},
    { 0xFFFFFFFF, 0},
};


struct cea_timing {
    uint8_t             interlace_mode;
    uint32_t            pfreq;
    uint8_t             ln;
    uint8_t             pixel_repeat;
    uint8_t             venc_pixel_repeat;

    uint32_t            hfreq;
    uint32_t            hactive;
    uint32_t            htotal;
    uint32_t            hblank;
    uint32_t            hfront;
    uint32_t            hsync;
    uint32_t            hback;
    uint8_t             hpol;

    uint32_t            vfreq;
    uint32_t            vactive;
    uint32_t            vtotal;
    uint32_t            vblank0;    // in case of interlace
    uint32_t            vblank1;    // vblank0 + 1 for interlace
    uint32_t            vfront;
    uint32_t            vsync;
    uint32_t            vback;
    uint8_t             vpol;
};

/*
struct hw_enc_clk_val_group {
    enum hdmi_vic group[GROUP_MAX];
    unsigned viu_path;
    enum viu_type viu_type;
    unsigned hpll_clk_out;
    unsigned od1;
    unsigned od2;
    unsigned od3;
    unsigned vid_pll_div;
    unsigned vid_clk_div;
    unsigned hdmi_tx_pixel_div;
    unsigned encp_div;
    unsigned enci_div;
};

 */

#define VID_PLL_DIV_1      0
#define VID_PLL_DIV_2      1
#define VID_PLL_DIV_3      2
#define VID_PLL_DIV_3p5    3
#define VID_PLL_DIV_3p75   4
#define VID_PLL_DIV_4      5
#define VID_PLL_DIV_5      6
#define VID_PLL_DIV_6      7
#define VID_PLL_DIV_6p25   8
#define VID_PLL_DIV_7      9
#define VID_PLL_DIV_7p5    10
#define VID_PLL_DIV_12     11
#define VID_PLL_DIV_14     12
#define VID_PLL_DIV_15     13
#define VID_PLL_DIV_2p5    14

enum viu_type {
    VIU_ENCL = 0,
    VIU_ENCI,
    VIU_ENCP,
    VIU_ENCT,
};

struct pll_param {
    uint32_t mode;
    uint32_t viu_channel;
    uint32_t viu_type;
    uint32_t hpll_clk_out;
    uint32_t od1;
    uint32_t od2;
    uint32_t od3;
    uint32_t vid_pll_div;
    uint32_t vid_clk_div;
    uint32_t hdmi_tx_pixel_div;
    uint32_t encp_div;
    uint32_t enci_div;
};

struct hdmi_param {
    uint16_t                         vic;
    uint8_t                         aspect_ratio;
    uint8_t                         colorimety;
    const struct reg_val_pair*      enc_lut;
    uint8_t                         phy_mode;
    struct pll_param                pll_p_24b;
    struct pll_param                pll_p_30b;
    struct pll_param                pll_p_36b;
    struct pll_param                pll_p_48b; // not supported
    struct cea_timing               timings;
};

#define HDMI_COLOR_DEPTH_24B    4
#define HDMI_COLOR_DEPTH_30B    5
#define HDMI_COLOR_DEPTH_36B    6
#define HDMI_COLOR_DEPTH_48B    7

#define HDMI_COLOR_FORMAT_RGB   0
#define HDMI_COLOR_FORMAT_444   1

#define HDMI_ASPECT_RATIO_4x3   1
#define HDMI_ASPECT_RATIO_16x9  2

#define HDMI_COLORIMETY_ITU601  1
#define HDMI_COLORIMETY_ITU709  2

/* VIC lookup */
 #define VIC_720x480p_60Hz_4x3                  2
 #define VIC_720x480p_60Hz_16x9                 3
 #define VIC_1280x720p_60Hz_16x9                4
 #define VIC_1920x1080i_60Hz_16x9               5
 #define VIC_720x480i_60Hz_4x3                  6
 #define VIC_720x480i_60Hz_16x9                 7
 #define VIC_720x240p_60Hz_4x3                  8
 #define VIC_720x240p_60Hz_16x9                 9
 #define VIC_2880x480i_60Hz_4x3                 10
 #define VIC_2880x480i_60Hz_16x9                11
 #define VIC_2880x240p_60Hz_4x3                 12
 #define VIC_2880x240p_60Hz_16x9                13
 #define VIC_1440x480p_60Hz_4x3                 14
 #define VIC_1440x480p_60Hz_16x9                15
 #define VIC_1920x1080p_60Hz_16x9               16
 #define VIC_720x576p_50Hz_4x3                  17
 #define VIC_720x576p_50Hz_16x9                 18
 #define VIC_1280x720p_50Hz_16x9                19
 #define VIC_1920x1080i_50Hz_16x9               20
 #define VIC_720x576i_50Hz_4x3                  21
 #define VIC_720x576i_50Hz_16x9                 22
 #define VIC_720x288p_50Hz_4x3                  23
 #define VIC_720x288p_50Hz_16x9                 24
 #define VIC_2880x576i_50Hz_4x3                 25
 #define VIC_2880x576i_50Hz_16x9                26
 #define VIC_2880x288p_50Hz_4x3                 27
 #define VIC_2880x288p_50Hz_16x9                28
 #define VIC_1440x576p_50Hz_4x3                 29
 #define VIC_1440x576p_50Hz_16x9                30
 #define VIC_1920x1080p_50Hz_16x9               31
 #define VIC_1920x1080p_24Hz_16x9               32
 #define VIC_1920x1080p_25Hz_16x9               33
 #define VIC_1920x1080p_30Hz_16x9               34
 #define VIC_2880x480p_60Hz_4x3                 35
 #define VIC_2880x480p_60Hz_16x9                36
 #define VIC_2880x576p_50Hz_4x3                 37
 #define VIC_2880x576p_50Hz_16x9                38
 #define VIC_1920x1080i_1250_50Hz_16x9          39
 #define VIC_1920x1080i_100Hz_16x9              40
 #define VIC_1280x720p_100Hz_16x9               41
 #define VIC_720x576p_100Hz_4x3                 42
 #define VIC_720x576p_100Hz_16x9                43
 #define VIC_720x576i_100Hz_4x3                 44
 #define VIC_720x576i_100Hz_16x9                45
 #define VIC_1920x1080i_120Hz_16x9              46
 #define VIC_1280x720p_120Hz_16x9               47
 #define VIC_720x480p_120Hz_4x3                 48
 #define VIC_720x480p_120Hz_16x9                49
 #define VIC_720x480i_120Hz_4x3                 50
 #define VIC_720x480i_120Hz_16x9                51
 #define VIC_720x576p_200Hz_4x3                 52
 #define VIC_720x576p_200Hz_16x9                53
 #define VIC_720x576i_200Hz_4x3                 54
 #define VIC_720x576i_200Hz_16x9                55
 #define VIC_720x480p_240Hz_4x3                 56
 #define VIC_720x480p_240Hz_16x9                57
 #define VIC_720x480i_240Hz_4x3                 58
 #define VIC_720x480i_240Hz_16x9                59
 #define VIC_1280x720p_24Hz_16x9                60
 #define VIC_1280x720p_25Hz_16x9                61
 #define VIC_1280x720p_30Hz_16x9                62
 #define VIC_1920x1080p_120Hz_16x9              63
 #define VIC_1920x1080p_100Hz_16x9              64
 #define VESA_OFFSET                            300
 #define VIC_VESA_640x480p_60Hz_4x3             300
 #define VIC_VESA_1280x800p_60Hz_16x9           301
 #define VIC_VESA_1280x1024p_60Hz_5x4           302

void hdmitx_writereg(vim2_display_t* display, uint32_t addr, uint32_t data);
uint32_t hdmitx_readreg(vim2_display_t* display, uint32_t addr);
zx_status_t init_hdmi_hardware(vim2_display_t* display);
void dump_regs(vim2_display_t* display);
zx_status_t init_hdmi_interface(vim2_display_t* display, const struct hdmi_param* p);
void hdmi_test(vim2_display_t* display, uint32_t width);
zx_status_t configure_pll(vim2_display_t* display, const struct hdmi_param* p, const struct pll_param* pll);