// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define KB(x)                                               (x << 10)
#define MB(x)                                               (x << 20)
#define GB(x)                                               (x << 30)

/* iMX8M Top Level A53 memory map */
#define IMX8M_PCIE_1_BASE                                   0x18000000
#define IMX8M_PCIE_1_LENGTH                                 MB(128)
#define IMX8M_PCIE_2_BASE                                   0x20000000
#define IMX8M_PCIE_2_LENGTH                                 MB(128)
#define IMX8M_A53_DAP_BASE                                  0x28000000
#define IMX8M_A53_DAP_LENGHT                                MB(16)
#define IMX8M_GPU_BASE                                      0x38000000
#define IMX8M_GPU_LENGTH                                    KB(64)
#define IMX8M_USB1_BASE                                     0x38100000
#define IMX8M_USB1_LENGTH                                   MB(1)
#define IMX8M_USB2_BASE                                     0x38200000
#define IMX8M_USB2_LENGTH                                   MB(1)
#define IMX8M_VPU_BASE                                      0x38300000
#define IMX8M_VPU_LENGTH                                    MB(2)
#define IMX8M_GIC_BASE                                      0x38800000
#define IMX8M_GIC_LENGTH                                    MB(1)

/* iMX8M AIPS (Peripheral) Memory */
#define IMX8M_AIPS_LENGTH                                   KB(64)
#define IMX8M_AIPS_GPIO1_BASE                               0x30200000
#define IMX8M_AIPS_GPIO2_BASE                               0x30210000
#define IMX8M_AIPS_GPIO3_BASE                               0x30220000
#define IMX8M_AIPS_GPIO4_BASE                               0x30230000
#define IMX8M_AIPS_GPIO5_BASE                               0x30240000
#define IMX8M_AIPS_WDOG1_BASE                               0x30280000
#define IMX8M_AIPS_WDOG2_BASE                               0x30290000
#define IMX8M_AIPS_WDOG3_BASE                               0x302A0000
#define IMX8M_AIPS_GPT1_BASE                                0x302D0000
#define IMX8M_AIPS_GPT2_BASE                                0x302E0000
#define IMX8M_AIPS_GPT3_BASE                                0x302F0000
#define IMX8M_AIPS_IOMUXC_BASE                              0x30330000
#define IMX8M_AIPS_IOMUXC_GPR_BASE                          0x30340000
#define IMX8M_AIPS_CCM_BASE                                 0x30380000
#define IMX8M_AIPS_GPC_BASE                                 0x303A0000
#define IMX8M_AIPS_PWM1_BASE                                0x30660000
#define IMX8M_AIPS_PWM2_BASE                                0x30670000
#define IMX8M_AIPS_PWM3_BASE                                0x30680000
#define IMX8M_AIPS_PWM4_BASE                                0x30690000
#define IMX8M_AIPS_GPT6_BASE                                0x306E0000
#define IMX8M_AIPS_GPT5_BASE                                0x306F0000
#define IMX8M_AIPS_GPT4_BASE                                0x30700000
#define IMX8M_AIPS_SPDIF1_BASE                              0x30810000
#define IMX8M_AIPS_ECSPI1_BASE                              0x30820000
#define IMX8M_AIPS_ECSPI2_BASE                              0x30830000
#define IMX8M_AIPS_ECSPI3_BASE                              0x30840000
#define IMX8M_AIPS_UART1_BASE                               0x30860000
#define IMX8M_AIPS_UART3_BASE                               0x30880000
#define IMX8M_AIPS_UART2_BASE                               0x30890000
#define IMX8M_AIPS_SPDIF2_BASE                              0x308A0000
#define IMX8M_AIPS_MIPI_PHY_BASE                            0x30A00000
#define IMX8M_AIPS_MIPI_DSI_BASE                            0x30A10000
#define IMX8M_AIPS_I2C1_BASE                                0x30A20000
#define IMX8M_AIPS_I2C2_BASE                                0x30A30000
#define IMX8M_AIPS_I2C3_BASE                                0x30A40000
#define IMX8M_AIPS_I2C4_BASE                                0x30A50000
#define IMX8M_AIPS_UART4_BASE                               0x30A60000
#define IMX8M_AIPS_USDHC1_BASE                              0x30B40000
#define IMX8M_AIPS_USDHC2_BASE                              0x30B50000
#define IMX8M_AIPS_QSPI_BASE                                0x30BB0000
#define IMX8M_AIPS_ENET1_BASE                               0x30BE0000
#define IMX8M_AIPS_DC_MST0_BASE                             0x32E00000
#define IMX8M_AIPS_DC_MST1_BASE                             0x32E10000
#define IMX8M_AIPS_DC_MST2_BASE                             0x32E20000
#define IMX8M_AIPS_DC_MST3_BASE                             0x32E30000

 #define IMX8M_A53_INTR_BOOT                                (32 + 0)
 #define IMX8M_A53_INTR_DAP                                 (32 + 1)
 #define IMX8M_A53_INTR_SDMA1                               (32 + 2)
 #define IMX8M_A53_INTR_GPU                                 (32 + 3)
 #define IMX8M_A53_INTR_SNVS_LP_WRAPPER                     (32 + 4)
 #define IMX8M_A53_INTR_LCDIF                               (32 + 5)
 #define IMX8M_A53_INTR_SPDIF1                              (32 + 6)
 #define IMX8M_A53_INTR_H264DEC                             (32 + 7)
 #define IMX8M_A53_INTR_VPUDMA                              (32 + 8)
 #define IMX8M_A53_INTR_QOS                                 (32 + 9)
 #define IMX8M_A53_INTR_WDOG3                               (32 + 10)
 #define IMX8M_A53_INTR_HS                                  (32 + 11)
 #define IMX8M_A53_INTR_APBHDMA                             (32 + 12)
 #define IMX8M_A53_INTR_SPDIF2                              (32 + 13)
 #define IMX8M_A53_INTR_SPDIF2                              (32 + 13)
 #define IMX8M_A53_INTR_RAWNAND_BCH_COMP                    (32 + 14)
 #define IMX8M_A53_INTR_RAWNAND_GPMI_TO                     (32 + 15)
 #define IMX8M_A53_INTR_HDMI_IPS                            (32 + 16)
 #define IMX8M_A53_INTR_HDMI_IPSx                           (32 + 17)
 #define IMX8M_A53_INTR_HDMI_IPSy                           (32 + 18)
 #define IMX8M_A53_INTR_SNVS_HP_WRAPPER_NOTZ                (32 + 19)
 #define IMX8M_A53_INTR_SNVS_HP_WRAPPER_TZ                  (32 + 20)
 #define IMX8M_A53_INTR_CSU                                 (32 + 21)
 #define IMX8M_A53_INTR_USDHC1                              (32 + 22)
 #define IMX8M_A53_INTR_USDHC2                              (32 + 23)
 #define IMX8M_A53_INTR_DC8000_CONTROL                      (32 + 24)
 #define IMX8M_A53_INTR_DTRC_WRAPPER                        (32 + 25)
 #define IMX8M_A53_INTR_UART1                               (32 + 26)
 #define IMX8M_A53_INTR_UART2                               (32 + 27)
 #define IMX8M_A53_INTR_UART3                               (32 + 28)
 #define IMX8M_A53_INTR_UART4                               (32 + 29)
 #define IMX8M_A53_INTR_VP9DEC                              (32 + 30)
 #define IMX8M_A53_INTR_ECSPI1                              (32 + 31)
 #define IMX8M_A53_INTR_ECSPI2                              (32 + 32)
 #define IMX8M_A53_INTR_ECSPI3                              (32 + 33)
 #define IMX8M_A53_INTR_MIPI_DSI                            (32 + 34)
 #define IMX8M_A53_INTR_I2C1                                (32 + 35)
 #define IMX8M_A53_INTR_I2C2                                (32 + 36)
 #define IMX8M_A53_INTR_I2C3                                (32 + 37)
 #define IMX8M_A53_INTR_I2C4                                (32 + 38)
 #define IMX8M_A53_INTR_RDC                                 (32 + 39)
 #define IMX8M_A53_INTR_USB1                                (32 + 40)
 #define IMX8M_A53_INTR_USB2                                (32 + 41)
 #define IMX8M_A53_INTR_CSI1                                (32 + 42)
 #define IMX8M_A53_INTR_CSI2                                (32 + 43)
 #define IMX8M_A53_INTR_MIPI_CSI1                           (32 + 44)
 #define IMX8M_A53_INTR_MIPI_CSI2                           (32 + 45)
 #define IMX8M_A53_INTR_GPT6                                (32 + 46)
 #define IMX8M_A53_INTR_SCTR0                               (32 + 47)
 #define IMX8M_A53_INTR_SCTR1                               (32 + 48)
 #define IMX8M_A53_INTR_ANAMIX_ALARM                        (32 + 49)
 #define IMX8M_A53_INTR_ANAMIX_CRIT_ALARM                   (32 + 49)
 #define IMX8M_A53_INTR_RESERVED                            (32 + 49)
 #define IMX8M_A53_INTR_SAI3_RX                             (32 + 50)
 #define IMX8M_A53_INTR_SAI3_RX_ASYNC                       (32 + 50)
 #define IMX8M_A53_INTR_SAI3_TX                             (32 + 50)
 #define IMX8M_A53_INTR_SAI3_TX_ASYNC                       (32 + 50)
 #define IMX8M_A53_INTR_GPT5                                (32 + 51)
 #define IMX8M_A53_INTR_GPT4                                (32 + 52)
 #define IMX8M_A53_INTR_GPT3                                (32 + 53)
 #define IMX8M_A53_INTR_GPT2                                (32 + 54)
 #define IMX8M_A53_INTR_GPT1                                (32 + 55)
 #define IMX8M_A53_INTR_GPIO1_INT7                          (32 + 56)
 #define IMX8M_A53_INTR_GPIO1_INT6                          (32 + 57)
 #define IMX8M_A53_INTR_GPIO1_INT5                          (32 + 58)
 #define IMX8M_A53_INTR_GPIO1_INT4                          (32 + 59)
 #define IMX8M_A53_INTR_GPIO1_INT3                          (32 + 60)
 #define IMX8M_A53_INTR_GPIO1_INT2                          (32 + 61)
 #define IMX8M_A53_INTR_GPIO1_INT1                          (32 + 62)
 #define IMX8M_A53_INTR_GPIO1_INT0                          (32 + 63)
 #define IMX8M_A53_INTR_GPIO1_INT_COMB_0_15                 (32 + 64)
 #define IMX8M_A53_INTR_GPIO1_INT_COMP_16_31                (32 + 65)
 #define IMX8M_A53_INTR_GPIO2_INT_COMB_0_15                 (32 + 66)
 #define IMX8M_A53_INTR_GPIO2_INT_COMP_16_31                (32 + 67)
 #define IMX8M_A53_INTR_GPIO3_INT_COMB_0_15                 (32 + 68)
 #define IMX8M_A53_INTR_GPIO3_INT_COMP_16_31                (32 + 69)
 #define IMX8M_A53_INTR_GPIO4_INT_COMB_0_15                 (32 + 70)
 #define IMX8M_A53_INTR_GPIO4_INT_COMP_16_31                (32 + 71)
 #define IMX8M_A53_INTR_GPIO5_INT_COMB_0_15                 (32 + 72)
 #define IMX8M_A53_INTR_GPIO5_INT_COMP_16_31                (32 + 73)
 #define IMX8M_A53_INTR_PCIE_CTRL2                          (32 + 74)
 #define IMX8M_A53_INTR_PCIE_CTRL2x                         (32 + 75)
 #define IMX8M_A53_INTR_PCIE_CTRL2y                         (32 + 76)
 #define IMX8M_A53_INTR_PCIE_CTRL2z                         (32 + 77)
 #define IMX8M_A53_INTR_WDOG1                               (32 + 78)
 #define IMX8M_A53_INTR_WDOG2                               (32 + 79)
 #define IMX8M_A53_INTR_PCIE_CTRL2zz                        (32 + 80)
 #define IMX8M_A53_INTR_PWM1                                (32 + 81)
 #define IMX8M_A53_INTR_PWM2                                (32 + 82)
 #define IMX8M_A53_INTR_PWM3                                (32 + 83)
 #define IMX8M_A53_INTR_PWM4                                (32 + 84)
 #define IMX8M_A53_INTR_CCMSRCGPCMIX_CCM1                   (32 + 85)
 #define IMX8M_A53_INTR_CCMSRCGPCMIX_CMM2                   (32 + 86)
 #define IMX8M_A53_INTR_CCMSRCGPCMIX_GPC1                   (32 + 87)
 #define IMX8M_A53_INTR_MU                                  (32 + 88)
 #define IMX8M_A53_INTR_CCMSRCGPCMIX                        (32 + 89)
 #define IMX8M_A53_INTR_SAI5_RX                             (32 + 90)
 #define IMX8M_A53_INTR_SAI5_RX_ASYNC                       (32 + 90)
 #define IMX8M_A53_INTR_SAI5_TX                             (32 + 90)
 #define IMX8M_A53_INTR_SAI5_TX_ASYNC                       (32 + 90)
 #define IMX8M_A53_INTR_SAI6_RX                             (32 + 90)
 #define IMX8M_A53_INTR_SAI6_RX_ASYNC                       (32 + 90)
 #define IMX8M_A53_INTR_SAI6_TX                             (32 + 90)
 #define IMX8M_A53_INTR_SAI6_TX_ASYNC                       (32 + 90)
 #define IMX8M_A53_INTR_RTIC                                (32 + 91)
 #define IMX8M_A53_INTR_CPU_PMUIRQ_0                        (32 + 92)
 #define IMX8M_A53_INTR_CPU_PMUIRQ_1                        (32 + 92)
 #define IMX8M_A53_INTR_CPU_PMUIRQ_2                        (32 + 92)
 #define IMX8M_A53_INTR_CPU_PMUIRQ_3                        (32 + 92)
 #define IMX8M_A53_INTR_CPU_NCTIIRQ_0                       (32 + 93)
 #define IMX8M_A53_INTR_CPU_NCTIIRQ_2                       (32 + 93)
 #define IMX8M_A53_INTR_CPU_NCTIIRQ_3                       (32 + 93)
 #define IMX8M_A53_INTR_CPU_NCTIIRQ_4                       (32 + 93)
 #define IMX8M_A53_INTR_CCMSRCGPCMIX_WDOG                   (32 + 94)
 #define IMX8M_A53_INTR_SAI1_RX                             (32 + 95)
 #define IMX8M_A53_INTR_SAI1_RX_ASYNC                       (32 + 95)
 #define IMX8M_A53_INTR_SAI1_TX                             (32 + 95)
 #define IMX8M_A53_INTR_SAI1_TX_ASYNC                       (32 + 95)
 #define IMX8M_A53_INTR_SAI2_RX                             (32 + 96)
 #define IMX8M_A53_INTR_SAI2_RX_ASYNC                       (32 + 96)
 #define IMX8M_A53_INTR_SAI2_TX                             (32 + 96)
 #define IMX8M_A53_INTR_SAI2_TX_ASYNC                       (32 + 96)
 #define IMX8M_A53_INTR_MU_M4                               (32 + 97)
 #define IMX8M_A53_INTR_DDR                                 (32 + 98)
 #define IMX8M_A53_INTR_SAI4_RX                             (32 + 100)
 #define IMX8M_A53_INTR_SAI4_RX_ASYNC                       (32 + 100)
 #define IMX8M_A53_INTR_SAI4_TX                             (32 + 100)
 #define IMX8M_A53_INTR_SAI4_TX_ASYNC                       (32 + 100)
 #define IMX8M_A53_INTR_CPU_ERR_AXI                         (32 + 101)
 #define IMX8M_A53_INTR_CPU_L2_RAM_ECC                      (32 + 102)
 #define IMX8M_A53_INTR_SDMA2                               (32 + 103)
 #define IMX8M_A53_INTR_RESERVED4                           (32 + 104)
 #define IMX8M_A53_INTR_CAAM_WRAPPER                        (32 + 105)
 #define IMX8M_A53_INTR_CAAM_WRAPPERx                       (32 + 106)
 #define IMX8M_A53_INTR_QSPI                                (32 + 107)
 #define IMX8M_A53_INTR_TZASC                               (32 + 108)
 #define IMX8M_A53_INTR_RESERVED1                           (32 + 109)
 #define IMX8M_A53_INTR_RESERVED2                           (32 + 110)
 #define IMX8M_A53_INTR_RESERVED3                           (32 + 111)
 #define IMX8M_A53_INTR_PERFMON1                            (32 + 112)
 #define IMX8M_A53_INTR_PERFMON2                            (32 + 113)
 #define IMX8M_A53_INTR_CAAM_WRAPPER_JQ                     (32 + 114)
 #define IMX8M_A53_INTR_CAAM_WRAPPER_RECOVER                (32 + 115)
 #define IMX8M_A53_INTR_HS_IRQ                              (32 + 116)
 #define IMX8M_A53_INTR_HEVCDEC                             (32 + 117)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_BUFFER_DONE           (32 + 118)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_FRAME_DONE            (32 + 118)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_BUFFER_DONE           (32 + 118)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_FRAME_DONE            (32 + 118)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_BUFFER_DONEx          (32 + 119)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_FRAME_DONEx           (32 + 119)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_BUFFER_DONEx          (32 + 119)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_FRAME_DONEx           (32 + 119)
 #define IMX8M_A53_INTR_ENET1_MAC0_PERI_TIMER_OF            (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_TIME_STAMP_AVAIL         (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_PAYLOAD_RX_ERROR         (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_FIFO_UNDERRUN         (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_COLL_RETRY_LIMIT         (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_LATE_COLLISION           (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_ETHERNET_BUS_ERROR       (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_MII_DATA_TRANSFER        (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_BUFFER_DONEy          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_FRAME_DONEy           (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_BUFFER_DONEy          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_TX_FRAME_DONEy           (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_GRACEFUL_STOP_           (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_BABBLING_TX_ERR          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_BABBLING_RX_ERR          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_FLUSH_FRAME0          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_FLUSH_FRAME1          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_RX_FLUSH_FRAME2          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_WAKEUP_REQUEST_SYNC      (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_BABBLING_RX_ERR          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_MAC0_WAKEUP_REQ_SYNC          (32 + 120)
 #define IMX8M_A53_INTR_ENET1_1588_INTR                     (32 + 121)
 #define IMX8M_A53_INTR_PCIE_CTRL1                          (32 + 122)
 #define IMX8M_A53_INTR_PCIE_CTRL1x                         (32 + 123)
 #define IMX8M_A53_INTR_PCIE_CTRL1y                         (32 + 124)
 #define IMX8M_A53_INTR_PCIE_CTRL1z                         (32 + 125)
 #define IMX8M_A53_INTR_RESERVED5                           (32 + 126)
 #define IMX8M_A53_INTR_PCIE_CTRL1zz                        (32 + 127)

/* USB PHY CTRL Registers (undocumented) */
#define USB_PHY_CTRL0               (0xF0040)
#define PHY_CTRL0_REF_SSP_EN        (1 << 2)
#define USB_PHY_CTRL1               (0xF0044)
#define PHY_CTRL1_RESET             (1 << 0)
#define PHY_CTRL1_ATERESET          (1 << 3)
#define PHY_CTRL1_VDATSRCENB0       (1 << 19)
#define PHY_CTRL1_VDATDETENB0       (1 << 20)
#define USB_PHY_CTRL2               (0xF0048)
#define PHY_CTRL2_TXENABLEN0        (1 << 8)
