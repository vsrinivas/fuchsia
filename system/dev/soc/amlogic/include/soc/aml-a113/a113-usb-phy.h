// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// size of phy port register block
#define PHY_REGISTER_SIZE 32
#define U2P_R0_OFFSET   0
#define U2P_R1_OFFSET   4
#define U2P_R2_OFFSET   8

#define USB_R0_OFFSET   0
#define USB_R1_OFFSET   4
#define USB_R2_OFFSET   8
#define USB_R3_OFFSET   12
#define USB_R4_OFFSET   16
#define USB_R5_OFFSET   20

#define U2P_R0_BYPASS_SEL                       (1 << 0)
#define U2P_R0_BYPASS_DM_EN                     (1 << 1)
#define U2P_R0_BYPASS_DP_EN                     (1 << 2)
#define U2P_R0_TXBITSTUFFENH                    (1 << 3)
#define U2P_R0_TXBITSTUFFEN                     (1 << 4)
#define U2P_R0_DMPULLDOWN                       (1 << 5)
#define U2P_R0_DPPULLDOWN                       (1 << 6)
#define U2P_R0_VBUSVLDEXTSEL                    (1 << 7)
#define U2P_R0_VBUSVLDEXT                       (1 << 8)
#define U2P_R0_ADP_PRB_EN                       (1 << 9)
#define U2P_R0_ADP_DISCHRG                      (1 << 10)
#define U2P_R0_ADP_CHRG                         (1 << 11)
#define U2P_R0_DRVVBUS                          (1 << 12)
#define U2P_R0_IDPULLUP                         (1 << 13)
#define U2P_R0_LOOPBACKENB                      (1 << 14)
#define U2P_R0_OTGDISABLE                       (1 << 15)
#define U2P_R0_COMMONONN                        (1 << 16)
#define U2P_R0_FSEL_START                       17
#define U2P_R0_FSEL_BITS                        3
#define U2P_R0_REFCLKSEL_START                  20
#define U2P_R0_REFCLKSEL_BITS                   2
#define U2P_R0_POR                              (1 << 22)
#define U2P_R0_VATESTENB_START                  23
#define U2P_R0_VATESTENB_BITS                   2
#define U2P_R0_SET_IDDQ                         (1 << 25)
#define U2P_R0_ATE_RESET                        (1 << 26)
#define U2P_R0_FSV_MINUS                        (1 << 27)
#define U2P_R0_FSV_PLUS                         (1 << 28)
#define U2P_R0_BYPASS_DM_DATA                   (1 << 29)
#define U2P_R0_BYPASS_DP_DATA                   (1 << 30)

#define U2P_R1_BURN_IN_TEST                     (1 << 0)
#define U2P_R1_ACA_ENABLE                       (1 << 1)
#define U2P_R1_DCD_ENABLE                       (1 << 2)
#define U2P_R1_VDATSRCENB                       (1 << 3)
#define U2P_R1_VDATDETENB                       (1 << 4)
#define U2P_R1_CHRGSEL                          (1 << 5)
#define U2P_R1_TX_PREEMP_PULSE_TUNE             (1 << 6)
#define U2P_R1_TX_PREEMP_AMP_TUNE_START         7
#define U2P_R1_TX_PREEMP_AMP_TUNE_BITS          2
#define U2P_R1_TX_RES_TUNE_START                9
#define U2P_R1_TX_RES_TUNE_BITS                 2
#define U2P_R1_TX_RISE_TUNE_START               11
#define U2P_R1_TX_RISE_TUNE_BITS                2
#define U2P_R1_TX_VREF_TUNE_START               13
#define U2P_R1_TX_VREF_TUNE_BITS                4
#define U2P_R1_TX_FSLS_TUNE_START               17
#define U2P_R1_TX_FSLS_TUNE_BITS                4
#define U2P_R1_TX_HSXV_TUNE_START               21
#define U2P_R1_TX_HSXV_TUNE_BITS                2
#define U2P_R1_OTG_TUNE_START                   23
#define U2P_R1_OTG_TUNE_BITS                    3
#define U2P_R1_SQRX_TUNE_START                  26
#define U2P_R1_SQRX_TUNE_BITS                   3
#define U2P_R1_COMP_DIS_TUNE_START              29
#define U2P_R1_COMP_DIS_TUNE_BITS               3

#define U2P_R2_DATA_IN_START                    0
#define U2P_R2_DATA_IN_BITS                     4
#define U2P_R2_DATA_IN_EN_START                 4
#define U2P_R2_DATA_IN_EN_BITS                  4
#define U2P_R2_ADDR_START                       8
#define U2P_R2_ADDR_BITS                        4
#define U2P_R2_DATA_OUT_SEL                     (1 << 12)
#define U2P_R2_CLK                              (1 << 13)
#define U2P_R2_DATA_OUT_START                    14
#define U2P_R2_DATA_OUT_BITS                     4
#define U2P_R2_ACA_PIN_RANGE_C                  (1 << 18)
#define U2P_R2_ACA_PIN_RANGE_B                  (1 << 19)
#define U2P_R2_ACA_PIN_RANGE_A                  (1 << 20)
#define U2P_R2_ACA_PIN_GND                      (1 << 21)
#define U2P_R2_ACA_PIN_FLOAT                    (1 << 22)
#define U2P_R2_CHG_DET                          (1 << 23)
#define U2P_R2_DEVICE_SESS_VLD                  (1 << 24)
#define U2P_R2_ADP_PROBE                        (1 << 25)
#define U2P_R2_ADP_SENSE                        (1 << 26)
#define U2P_R2_SESSEND                          (1 << 27)
#define U2P_R2_VBUSVALID                        (1 << 28)
#define U2P_R2_BVALID                           (1 << 29)
#define U2P_R2_AVALID                           (1 << 30)
#define U2P_R2_IDDIG                            (1 << 31)

#define USB_R0_P30_FSEL_START                   0
#define USB_R0_P30_FSEL_BITS                    6
#define USB_R0_P30_PHY_RESET                    (1 << 6)
#define USB_R0_P30_TEST_POWERDOWN_HSP           (1 << 7)
#define USB_R0_P30_TEST_POWERDOWN_SSP           (1 << 8)
#define USB_R0_P30_ACJT_LEVEL_START             9
#define USB_R0_P30_ACJT_LEVEL_BITS              5
#define USB_R0_P30_TX_VBOOST_LVL_START          14
#define USB_R0_P30_TX_VBOOST_LVL_BITS           3
#define USB_R0_P30_LANE0_TX2RX_LOOPBK           (1 << 17)
#define USB_R0_P30_LANE0_EXT_PCLK_REQ           (1 << 18)
#define USB_R0_P30_PCS_RX_LOS_MASK_VAL_START    19
#define USB_R0_P30_PCS_RX_LOS_MASK_VAL_BITS     10
#define USB_R0_U2D_SS_SCALEDOWN_MODE_START      29
#define USB_R0_U2D_SS_SCALEDOWN_MODE_BITS       2
#define USB_R0_U2D_ACT                          (1 << 31)

#define USB_R1_U3H_BIGENDIAN_GS                 (1 << 0)
#define USB_R1_U3H_PME_EN                       (1 << 1)
#define USB_R1_U3H_HUB_PORT_OVERCURRENT_START   2
#define USB_R1_U3H_HUB_PORT_OVERCURRENT_BITS    5
#define USB_R1_U3H_HUB_PORT_PERM_ATTACH_START   7
#define USB_R1_U3H_HUB_PORT_PERM_ATTACH_BITS    5
#define USB_R1_U3H_HOST_U2_PORT_DISABLE_START   12
#define USB_R1_U3H_HOST_U2_PORT_DISABLE_BITS    4
#define USB_R1_U3H_HOST_U3_PORT_DISABLE         (1 << 16)
#define USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT  (1 << 17)
#define USB_R1_U3H_HOST_MSI_ENABLE              (1 << 18)
#define USB_R1_U3H_FLADJ_30MHZ_REG_START        19
#define USB_R1_U3H_FLADJ_30MHZ_REG_BITS         6
#define USB_R1_P30_PCS_TX_SWING_FULL_START      25
#define USB_R1_P30_PCS_TX_SWING_FULL            7

#define USB_R2_P30_CR_DATA_IN_START             0
#define USB_R2_P30_CR_DATA_IN_BITS              16
#define USB_R2_P30_CR_READ                      (1 << 16)
#define USB_R2_P30_CR_WRITE                     (1 << 17)
#define USB_R2_P30_CR_CAP_ADDR                  (1 << 18)
#define USB_R2_P30_CR_CAP_DATA                  (1 << 19)
#define USB_R2_P30_PCS_TX_DEEMPH_3P5DB_START    20
#define USB_R2_P30_PCS_TX_DEEMPH_3P5DB_BITS     6
#define USB_R2_P30_PCS_TX_DEEMPH_6DB_START      26
#define USB_R2_P30_PCS_TX_DEEMPH_6DB_BITS       6

#define USB_R3_P30_SSC_EN                       (1 << 0)
#define USB_R3_P30_SSC_RANGE_START              1
#define USB_R3_P30_SSC_RANGE_BITS               3
#define USB_R3_P30_SSC_REF_CLK_SEL_START        4
#define USB_R3_P30_SSC_REF_CLK_SEL_BITS         9
#define USB_R3_P30_REF_SSP_EN                   (1 << 13)
#define USB_R3_RESERVED14_START                 14
#define USB_R3_RESERVED14_BITS                  2
#define USB_R3_P30_LOS_BIAS_START               16
#define USB_R3_P30_LOS_BIAS_BITS                3
#define USB_R3_P30_LOS_LEVEL_START              19
#define USB_R3_P30_LOS_LEVEL_BITS               5
#define USB_R3_P30_MPLL_MULTIPLIER_START        24
#define USB_R3_P30_MPLL_MULTIPLIER_BITS         7

#define USB_R4_P21_PORTRESET0                   (1 << 0)
#define USB_R4_P21_SLEEPM0                      (1 << 1)
#define USB_R4_MEM_PD_START                     2
#define USB_R4_MEM_PD_BITS                      2
#define USB_R4_P21_ONLY                         (1 << 4)

#define USB_R5_IDDIG_SYNC                       (1 << 0)
#define USB_R5_IDDIG_REG                        (1 << 1)
#define USB_R5_IDDIG_CFG_START                  2
#define USB_R5_IDDIG_CFG_BITS                   2
#define USB_R5_IDDIG_EN0                        (1 << 4)
#define USB_R5_IDDIG_EN1                        (1 << 5)
#define USB_R5_IDDIG_CURR                       (1 << 6)
#define USB_R5_IDDIG_IRQ                        (1 << 7)
#define USB_R5_IDDIG_TH_START                   8
#define USB_R5_IDDIG_TH_BITS                    8
#define USB_R5_IDDIG_CNT_START                  16
#define USB_R5_IDDIG_CNT_BITS                   8

#define USB_R6_P30_CR_DATA_OUT_START            0
#define USB_R6_P30_CR_DATA_OUT_BITS             16
#define USB_R6_P30_CR_ACK                       (1 << 16)
