// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_HHI_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_HHI_REGS_H_

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

#include "hhi-regs.h"

#define HHI_GCLK_MPEG2 (0x52 << 2)
#define HHI_VID_CLK_CNTL (0x5f << 2)
#define HHI_HDMI_CLK_CNTL (0x73 << 2)
#define HHI_VDAC_CNTL0_G12A (0xbb << 2)
#define HHI_VDAC_CNTL1_G12A (0xbc << 2)

#define HHI_HDMI_PLL_CNTL (0xc8 << 2)
// HHI_HDMI_PLL_CNTL bit definitions
#define PLL_CNTL_LOCK (1 << 31)
#define PLL_CNTL_ENABLE (1 << 30)
#define G12A_PLL_CNTL_RESET (1 << 29)
#define PLL_CNTL_RESET (1 << 28)
#define PLL_CNTL_N(x) (x << 9)
#define PLL_CNTL_M_START (0)
#define PLL_CNTL_M_BITS (9)

// HHI_HDMI_PLL_CNTL1 bit definitions
#define PLL_CNTL1_DIV_FRAC_START (0)
#define PLL_CNTL1_DIV_FRAC_BITS (12)

#define HHI_HDMI_PLL_STS (0xce << 2)

#define HHI_HDMI_PHY_CNTL0 (0xe8 << 2)
#define HHI_HDMI_PHY_CNTL1 (0xe9 << 2)
#define HHI_HDMI_PHY_CNTL2 (0xea << 2)
#define HHI_HDMI_PHY_CNTL3 (0xeb << 2)
#define HHI_HDMI_PHY_CNTL4 (0xec << 2)
#define HHI_HDMI_PHY_CNTL5 (0xed << 2)

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_HHI_REGS_H_
