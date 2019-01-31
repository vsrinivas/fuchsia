// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// size of phy port register block
#define PHY_REGISTER_SIZE 32
#define U2P_R0_OFFSET   0x0
#define U2P_R1_OFFSET   0x4

#define USB_R0_OFFSET   0x80
#define USB_R1_OFFSET   0x84
#define USB_R2_OFFSET   0x88
#define USB_R3_OFFSET   0x8c
#define USB_R4_OFFSET   0x90
#define USB_R5_OFFSET   0x94

#define U2P_R0_HOST_DEVICE                          (1 << 0)
#define U2P_R0_POWER_OK                             (1 << 1)
#define U2P_R0_HOST_MODE                            (1 << 2)
#define U2P_R0_POR                                  (1 << 3)
#define U2P_R0_IDPULLUP0                            (1 << 4)
#define U2P_R0_DRVVBUS0                             (1 << 5)

#define U2P_R1_PHY_RDY                              (1 << 0)
#define U2P_R1_IDDIG0                               (1 << 1)
#define U2P_R1_OTGSESSVLD0                          (1 << 2)
#define U2P_R1_VBUSVALID0                           (1 << 3)

#define USB_R0_P30_LANE0_TX2RX_LOOPBACK             (1 << 17)
#define USB_R0_P30_LANE0_EXT_PCLK_REG               (1 << 18)
#define USB_R0_P30_PCS_RX_LOS_MASK_VAL              (1 << 19)   // 10 bits
#define USB_R0_U2D_SS_SCALEDOWN_MODE                (1 << 29)   // 2 bits
#define USB_R0_U2D_ACT                              (1 << 31)

#define USB_R1_U3H_BIGENDIAN_GS                     (1 << 0)
#define USB_R1_U3H_PME_EN                           (1 << 1)
#define USB_R1_U3H_HUB_PORT_OVERCURRENT             (1 << 2)    // 3 bits
#define USB_R1_U3H_HUB_PORT_PERM_ATTACH             (1 << 7)    // 3 bits
#define USB_R1_U3H_HOST_U2_PORT_DISABLE             (1 << 12)   // 2 bits
#define USB_R1_U3H_HOST_U3_PORT_DISABLE             (1 << 16)
#define USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT  (1 << 17)
#define USB_R1_U3H_HOST_MSI_ENABLE                  (1 << 18)
#define USB_R1_U3H_FLADJ_30MHZ_REG                  (1 << 19)   // 6 bits
#define USB_R1_P30_PCS_TX_SWING_FULL                (1 << 25)   // 7 bits

#define USB_R2_P30_PCS_TX_DEEMPH_3P5DB              (1 << 20)   // 6 bits
#define USB_R2_P30_PCS_TX_DEEMPH_6DB                (1 << 26)   // 6 bits

#define USB_R3_P30_SSC_EN                           (1 << 0)
#define USB_R3_P30_SSC_RANGE                        (1 << 1)    // 3 bits
#define USB_R3_P30_SSC_REF_CLK_SEL                  (1 << 4)    // 9 bits
#define USB_R3_P30_REF_SSP_EN                       (1 << 13)

#define USB_R4_P21_PORTRESET0                       (1 << 0)
#define USB_R4_P21_SLEEPM0                          (1 << 1)
#define USB_R4_MEM_PD                               (1 << 2)    // 2 bits
#define USB_R4_P21_ONLY                             (1 << 4)

#define USB_R5_IDDIG_SYNC                           (1 << 0)
#define USB_R5_IDDIG_REG                            (1 << 1)
#define USB_R5_IDDIG_CFG                            (1 << 2)    // 2 bits
#define USB_R5_IDDIG_EN0                            (1 << 4)
#define USB_R5_IDDIG_EN1                            (1 << 5)
#define USB_R5_IDDIG_CURR                           (1 << 6)
#define USB_R5_USB_IDDIG_IRQ                        (1 << 7)
#define USB_R5_IDDIG_TH                             (1 << 8)    // 8 bits
#define USB_R5_IDDIG_CNT                            (1 << 16)   // 8 bits
