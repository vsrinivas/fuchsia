// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_BUS_AML_MIPICSI_AML_MIPI_REGS_H_
#define SRC_CAMERA_DRIVERS_BUS_AML_MIPICSI_AML_MIPI_REGS_H_

// clang-format off
#define HI_CSI_PHY_CNTL0                    0x4C
#define HI_CSI_PHY_CNTL1                    0x50
#define HI_CSI_PHY_CNTL2                    0x54
#define HI_CSI_PHY_CNTL3                    0x58

#define MIPI_PHY_CTRL                       0x00
#define MIPI_PHY_CLK_LANE_CTRL              0x04
#define MIPI_PHY_DATA_LANE_CTRL             0x08
#define MIPI_PHY_DATA_LANE_CTRL1            0x0C
#define MIPI_PHY_TCLK_MISS                  0x10
#define MIPI_PHY_TCLK_SETTLE                0x14
#define MIPI_PHY_THS_EXIT                   0x18
#define MIPI_PHY_THS_SKIP                   0x1C
#define MIPI_PHY_THS_SETTLE                 0x20
#define MIPI_PHY_TINIT                      0x24
#define MIPI_PHY_TULPS_C                    0x28
#define MIPI_PHY_TULPS_S                    0x2C
#define MIPI_PHY_TMBIAS                     0x30
#define MIPI_PHY_TLP_EN_W                   0x34
#define MIPI_PHY_TLPOK                      0x38
#define MIPI_PHY_TWD_INIT                   0x3C
#define MIPI_PHY_TWD_HS                     0x40
#define MIPI_PHY_AN_CTRL0                   0x44
#define MIPI_PHY_AN_CTRL1                   0x48
#define MIPI_PHY_AN_CTRL2                   0x4C
#define MIPI_PHY_CLK_LANE_STS               0x50
#define MIPI_PHY_DATA_LANE0_STS             0x54
#define MIPI_PHY_DATA_LANE1_STS             0x58
#define MIPI_PHY_DATA_LANE2_STS             0x5C
#define MIPI_PHY_DATA_LANE3_STS             0x60
#define MIPI_PHY_INT_STS                    0x6C
#define MIPI_PHY_MUX_CTRL0                  0x184
#define MIPI_PHY_MUX_CTRL1                  0x188

#define MIPI_CSI_VERSION                    0x000
#define MIPI_CSI_N_LANES                    0x004
#define MIPI_CSI_PHY_SHUTDOWNZ              0x008
#define MIPI_CSI_DPHY_RSTZ                  0x00C
#define MIPI_CSI_CSI2_RESETN                0x010
#define MIPI_CSI_PHY_STAT                   0x014
#define MIPI_CSI_DATA_IDS_1                 0x018
#define MIPI_CSI_DATA_IDS_2                 0x01C

#define FRONTEND_BASE                       0x00004800

#define CSI2_CLK_RESET                      0x00
#define CSI2_GEN_CTRL0                      0x04
#define CSI2_GEN_CTRL1                      0x08
#define CSI2_X_START_END_ISP                0x0C
#define CSI2_Y_START_END_ISP                0x10
#define CSI2_X_START_END_MEM                0x14
#define CSI2_Y_START_END_MEM                0x18
#define CSI2_VC_MODE                        0x1C
#define CSI2_VC_MODE2_MATCH_MASK_L          0x20
#define CSI2_VC_MODE2_MATCH_MASK_H          0x24
#define CSI2_VC_MODE2_MATCH_TO_VC_L         0x28
#define CSI2_VC_MODE2_MATCH_TO_VC_H         0x2C
#define CSI2_VC_MODE2_MATCH_TO_IGNORE_L     0x30
#define CSI2_VC_MODE2_MATCH_TO_IGNORE_H     0x34
#define CSI2_DDR_START_PIX                  0x38
#define CSI2_DDR_START_PIX_ALT              0x3C
#define CSI2_DDR_STRIDE_PIX                 0x40
#define CSI2_DDR_START_OTHER                0x44
#define CSI2_DDR_START_OTHER_ALT            0x48
#define CSI2_DDR_MAX_BYTES_OTHER            0x4C
#define CSI2_INTERRUPT_CTRL_STAT            0x50

#define CSI2_GEN_STAT0                      0x80
#define CSI2_ERR_STAT0                      0x84
#define CSI2_PIC_SIZE_STAT                  0x88
#define CSI2_DDR_WPTR_STAT_PIX              0x8C
#define CSI2_DDR_WPTR_STAT_OTHER            0x90
#define CSI2_STAT_MEM_0                     0x94
#define CSI2_STAT_MEM_1                     0x98

#define CSI2_STAT_GEN_SHORT_08              0xA0
#define CSI2_STAT_GEN_SHORT_09              0xA4
#define CSI2_STAT_GEN_SHORT_0A              0xA8
#define CSI2_STAT_GEN_SHORT_0B              0xAC
#define CSI2_STAT_GEN_SHORT_0C              0xB0
#define CSI2_STAT_GEN_SHORT_0D              0xB4
#define CSI2_STAT_GEN_SHORT_0E              0xB8
#define CSI2_STAT_GEN_SHORT_0F              0xBC

#define RD_BASE                             0x00005000
#define MIPI_ADAPT_DDR_RD0_CNTL0            0x00
#define MIPI_ADAPT_DDR_RD0_CNTL1            0x04
#define MIPI_ADAPT_DDR_RD0_CNTL2            0x08
#define MIPI_ADAPT_DDR_RD0_CNTL3            0x0C
#define MIPI_ADAPT_DDR_RD1_CNTL0            0x40
#define MIPI_ADAPT_DDR_RD1_CNTL1            0x44
#define MIPI_ADAPT_DDR_RD1_CNTL2            0x48
#define MIPI_ADAPT_DDR_RD1_CNTL3            0x4C

#define PIXEL_BASE                          0x00005000
#define MIPI_ADAPT_PIXEL0_CNTL0             0x80
#define MIPI_ADAPT_PIXEL0_CNTL1             0x84
#define MIPI_ADAPT_PIXEL1_CNTL0             0x88
#define MIPI_ADAPT_PIXEL1_CNTL1             0x8C


#define ALIGN_BASE                          0x00005000
#define MIPI_ADAPT_ALIG_CNTL0               0xC0
#define MIPI_ADAPT_ALIG_CNTL1               0xC4
#define MIPI_ADAPT_ALIG_CNTL2               0xC8
#define MIPI_ADAPT_ALIG_CNTL6               0xD8
#define MIPI_ADAPT_ALIG_CNTL7               0xDC
#define MIPI_ADAPT_ALIG_CNTL8               0xE0
#define MIPI_OTHER_CNTL0                    0x100
#define MIPI_ADAPT_IRQ_MASK0                0x180
#define MIPI_ADAPT_IRQ_PENDING0             0x184

#define MISC_BASE                           0x00005000
// clang-format on

// CLK offsets.
#define HHI_MIPI_CSI_PHY_CLK_CNTL (0xD0 << 2)

#endif  // SRC_CAMERA_DRIVERS_BUS_AML_MIPICSI_AML_MIPI_REGS_H_
