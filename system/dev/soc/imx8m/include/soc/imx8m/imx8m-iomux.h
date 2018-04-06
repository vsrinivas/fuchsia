// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

/*
 * iomux_cfg_struct is a 64 bit field that contains all the values needed for pinmux.
 * [2:0]        :   MUXMODE: Select Pin functionality
 * [3]          :   SION: Software Input On Field
 * [6:4]        :   DSE: Drive Strength
 * [8:7]        :   SRE:Slew Rate Field
 * [9]          :   ODE: Open Drain Enable Field
 * [10]         :   PUE: Pull Up Enable Field
 * [11]         :   HYS: Schmitt Trigger Enable Field
 * [12]         :   LVTTL: Lvttl Enable Field
 * [15:13]      :   VSEL: Voltage Select Field
 * [16]         :   DAISY: Input Select Field
 * [23:17]      :   RSVD: Future Use
 * [35:24]      :   MUX_CTL_OFF: Start of MUX_CTL register from IOMUX Base Register
 * [47:36]      :   PAT_CTL_OFF: Start of PAT_CTL register from IOMUX Base Register
 * [60:48]      :   SEL_INP_OFF: Start of Input Select register from IOMUX Base Register
 * [64:61]      :   RSVD: Future Use
 */
typedef uint64_t iomux_cfg_struct;

/* iomux_cfg_struct bit field definition */
#define MUX_MODE_START        0
#define MUX_MODE_COUNT        3
#define SET_MUX_MODE_VAL(x)   \
                        ((x & ((1 << MUX_MODE_COUNT) - 1)) << MUX_MODE_START)
#define GET_MUX_MODE_VAL(x)   \
                        ((x >> MUX_MODE_START)  & ((1 << MUX_MODE_COUNT) - 1))

#define SION_START            3
#define SION_COUNT            1
#define SET_SION_VAL(x)   \
                        ((x & ((1 << SION_COUNT) - 1)) << SION_START)
#define GET_SION_VAL(x)   \
                        ((x >> SION_START)  & ((1 << SION_COUNT) - 1))

/* PAD CTRL Bit Defs */
#define DSE_START             4
#define DSE_COUNT             3
#define SET_DSE_VAL(x)    \
                        ((x & ((1 << DSE_COUNT) - 1)) << DSE_START)
#define GET_DSE_VAL(x)    \
                        ((x >> DSE_START)  & ((1 << DSE_COUNT) - 1))

#define SRE_START             7
#define SRE_COUNT             2
#define SET_SRE_VAL(x)    \
                        ((x & ((1 << SRE_COUNT) - 1)) << SRE_START)
#define GET_SRE_VAL(x)    \
                        ((x >> SRE_START)  & ((1 << SRE_COUNT) - 1))

#define ODE_START             9
#define ODE_COUNT             1
#define SET_ODE_VAL(x)    \
                        ((x & ((1 << ODE_COUNT) - 1)) << ODE_START)
#define GET_ODE_VAL(x)    \
                        ((x >> ODE_START)  & ((1 << ODE_COUNT) - 1))

#define PUE_START             10
#define PUE_COUNT             1
#define SET_PUE_VAL(x)    \
                        ((x & ((1 << PUE_COUNT) - 1)) << PUE_START)
#define GET_PUE_VAL(x)    \
                        ((x >> PUE_START)  & ((1 << PUE_COUNT) - 1))

#define HYS_START             11
#define HYS_COUNT             1
#define SET_HYS_VAL(x)    \
                        ((x & ((1 << HYS_COUNT) - 1)) << HYS_START)
#define GET_HYS_VAL(x)    \
                        ((x >> HYS_START)  & ((1 << HYS_COUNT) - 1))

#define LVTTL_START           12
#define LVTTL_COUNT           1
#define SET_LVTTL_VAL(x)  \
                        ((x & ((1 << LVTTL_COUNT) - 1)) << LVTTL_START)
#define GET_LVTTL_VAL(x)  \
                        ((x >> LVTTL_START)  & ((1 << LVTTL_COUNT) - 1))

#define VSEL_START            13
#define VSEL_COUNT            3
#define SET_VSEL_VAL(x)   \
                        ((x & ((1 << VSEL_COUNT) - 1)) << VSEL_START)
#define GET_VSEL_VAL(x)   \
                        ((x >> VSEL_START)  & ((1 << VSEL_COUNT) - 1))

#define DAISY_START           16
#define DAISY_COUNT           1
#define SET_DAISY_VAL(x)  \
                        ((x & ((1 << DAISY_COUNT) - 1)) << DAISY_START)
#define GET_DAISY_VAL(x)  \
                        ((x >> DAISY_START)  & ((1 << DAISY_COUNT) - 1))

#define MUX_CTL_OFF_START     24
#define MUX_CTL_OFF_COUNT     12
#define SET_MUX_CTL_OFF_VAL(x) \
                ((x & ((1 << MUX_CTL_OFF_COUNT) - 1)) << MUX_CTL_OFF_START)
#define GET_MUX_CTL_OFF_VAL(x) \
                ((x >> MUX_CTL_OFF_START)  & ((1 << MUX_CTL_OFF_COUNT) - 1))

#define PAD_CTL_OFF_START     36
#define PAD_CTL_OFF_COUNT     12
#define SET_PAD_CTL_OFF_VAL(x) \
                ((x & ((1 << PAD_CTL_OFF_COUNT) - 1)) << PAD_CTL_OFF_START)
#define GET_PAD_CTL_OFF_VAL(x) \
                ((x >> PAD_CTL_OFF_START)  & ((1 << PAD_CTL_OFF_COUNT) - 1))

#define SEL_INP_OFF_START     48
#define SEL_INP_OFF_COUNT     12
#define SET_SEL_INP_OFF_VAL(x) \
                ((x & ((1 << SEL_INP_OFF_COUNT) - 1)) << SEL_INP_OFF_START)
#define GET_SEL_INP_OFF_VAL(x) \
                ((x >> SEL_INP_OFF_START)  & ((1 << SEL_INP_OFF_COUNT) - 1))

/* end of iomux_cfg_struct bit field definition */

#define MAKE_PIN_CFG(mux_mode, sion, dse, sre, ode, pue, hys, lvttl, vsel, \
                        daisy, mux_ctl_off, pad_ctl_off, sel_inp_off) \
            SET_MUX_MODE_VAL(mux_mode)          | \
            SET_SION_VAL(sion)                  | \
            SET_DSE_VAL(dse)                    | \
            SET_SRE_VAL(sre)                    | \
            SET_ODE_VAL(ode)                    | \
            SET_PUE_VAL(pue)                    | \
            SET_HYS_VAL(hys)                    | \
            SET_LVTTL_VAL(lvttl)                | \
            SET_VSEL_VAL(vsel)                  | \
            SET_DAISY_VAL(daisy)                | \
            SET_MUX_CTL_OFF_VAL(mux_ctl_off)    | \
            SET_PAD_CTL_OFF_VAL(pad_ctl_off)    | \
            SET_SEL_INP_OFF_VAL(sel_inp_off)

#define MAKE_PIN_CFG_UART(mux_mode, mux_ctl_off, pad_ctl_off, sel_inp_off) \
            MAKE_PIN_CFG(mux_mode, 0, DSR_45_OHM, SRE_MEDIUM, 0, 0, 0, 0, 0, 0, \
                mux_ctl_off, pad_ctl_off, sel_inp_off)

#define MAKE_PIN_CFG_DEFAULT(mux_mode, mux_ctl_off) \
            MAKE_PIN_CFG(mux_mode, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                mux_ctl_off, 0x000ULL, 0x000ULL)


/* IMX8M IOMUX Register Offsets */
#define SW_MUX_CTL_PAD_GPIO1_IO00               0x0028ULL
#define SW_MUX_CTL_PAD_GPIO1_IO01               0x002CULL
#define SW_MUX_CTL_PAD_GPIO1_IO02               0x0030ULL
#define SW_MUX_CTL_PAD_GPIO1_IO03               0x0034ULL
#define SW_MUX_CTL_PAD_GPIO1_IO04               0x0038ULL
#define SW_MUX_CTL_PAD_GPIO1_IO05               0x003CULL
#define SW_MUX_CTL_PAD_GPIO1_IO06               0x0040ULL
#define SW_MUX_CTL_PAD_GPIO1_IO07               0x0044ULL
#define SW_MUX_CTL_PAD_GPIO1_IO08               0x0048ULL
#define SW_MUX_CTL_PAD_GPIO1_IO09               0x004CULL
#define SW_MUX_CTL_PAD_GPIO1_IO10               0x0050ULL
#define SW_MUX_CTL_PAD_GPIO1_IO11               0x0054ULL
#define SW_MUX_CTL_PAD_GPIO1_IO12               0x0058ULL
#define SW_MUX_CTL_PAD_GPIO1_IO13               0x005CULL
#define SW_MUX_CTL_PAD_GPIO1_IO14               0x0060ULL
#define SW_MUX_CTL_PAD_GPIO1_IO15               0x0064ULL
#define SW_MUX_CTL_PAD_ENET_MDC                 0x0068ULL
#define SW_MUX_CTL_PAD_ENET_MDIO                0x006CULL
#define SW_MUX_CTL_PAD_ENET_TD3                 0x0070ULL
#define SW_MUX_CTL_PAD_ENET_TD2                 0x0074ULL
#define SW_MUX_CTL_PAD_ENET_TD1                 0x0078ULL
#define SW_MUX_CTL_PAD_ENET_TD0                 0x007CULL
#define SW_MUX_CTL_PAD_ENET_TX_CTL              0x0080ULL
#define SW_MUX_CTL_PAD_ENET_TXC                 0x0084ULL
#define SW_MUX_CTL_PAD_ENET_RX_CTL              0x0088ULL
#define SW_MUX_CTL_PAD_ENET_RXC                 0x008CULL
#define SW_MUX_CTL_PAD_ENET_RD0                 0x0090ULL
#define SW_MUX_CTL_PAD_ENET_RD1                 0x0094ULL
#define SW_MUX_CTL_PAD_ENET_RD2                 0x0098ULL
#define SW_MUX_CTL_PAD_ENET_RD3                 0x009CULL
#define SW_MUX_CTL_PAD_SD1_CLK                  0x00A0ULL
#define SW_MUX_CTL_PAD_SD1_CMD                  0x00A4ULL
#define SW_MUX_CTL_PAD_SD1_DATA0                0x00A8ULL
#define SW_MUX_CTL_PAD_SD1_DATA1                0x00ACULL
#define SW_MUX_CTL_PAD_SD1_DATA2                0x00B0ULL
#define SW_MUX_CTL_PAD_SD1_DATA3                0x00B4ULL
#define SW_MUX_CTL_PAD_SD1_DATA4                0x00B8ULL
#define SW_MUX_CTL_PAD_SD1_DATA5                0x00BCULL
#define SW_MUX_CTL_PAD_SD1_DATA6                0x00C0ULL
#define SW_MUX_CTL_PAD_SD1_DATA7                0x00C4ULL
#define SW_MUX_CTL_PAD_SD1_RESET_B              0x00C8ULL
#define SW_MUX_CTL_PAD_SD1_STROBE               0x00CCULL
#define SW_MUX_CTL_PAD_SD2_CD_B                 0x00D0ULL
#define SW_MUX_CTL_PAD_SD2_CLK                  0x00D4ULL
#define SW_MUX_CTL_PAD_SD2_CMD                  0x00D8ULL
#define SW_MUX_CTL_PAD_SD2_DATA0                0x00DCULL
#define SW_MUX_CTL_PAD_SD2_DATA1                0x00E0ULL
#define SW_MUX_CTL_PAD_SD2_DATA2                0x00E4ULL
#define SW_MUX_CTL_PAD_SD2_DATA3                0x00E8ULL
#define SW_MUX_CTL_PAD_SD2_RESET_B              0x00ECULL
#define SW_MUX_CTL_PAD_SD2_WP                   0x00F0ULL
#define SW_MUX_CTL_PAD_NAND_ALE                 0x00F4ULL
#define SW_MUX_CTL_PAD_NAND_CE0_B               0x00F8ULL
#define SW_MUX_CTL_PAD_NAND_CE1_B               0x00FCULL
#define SW_MUX_CTL_PAD_NAND_CE2_B               0x0100ULL
#define SW_MUX_CTL_PAD_NAND_CE3_B               0x0104ULL
#define SW_MUX_CTL_PAD_NAND_CLE                 0x0108ULL
#define SW_MUX_CTL_PAD_NAND_DATA00              0x010CULL
#define SW_MUX_CTL_PAD_NAND_DATA01              0x0110ULL
#define SW_MUX_CTL_PAD_NAND_DATA02              0x0114ULL
#define SW_MUX_CTL_PAD_NAND_DATA03              0x0118ULL
#define SW_MUX_CTL_PAD_NAND_DATA04              0x011CULL
#define SW_MUX_CTL_PAD_NAND_DATA05              0x0120ULL
#define SW_MUX_CTL_PAD_NAND_DATA06              0x0124ULL
#define SW_MUX_CTL_PAD_NAND_DATA07              0x0128ULL
#define SW_MUX_CTL_PAD_NAND_DQS                 0x012CULL
#define SW_MUX_CTL_PAD_NAND_RE_B                0x0130ULL
#define SW_MUX_CTL_PAD_NAND_READY_B             0x0134ULL
#define SW_MUX_CTL_PAD_NAND_WE_B                0x0138ULL
#define SW_MUX_CTL_PAD_NAND_WP_B                0x013CULL
#define SW_MUX_CTL_PAD_SAI5_RXFS                0x0140ULL
#define SW_MUX_CTL_PAD_SAI5_RXC                 0x0144ULL
#define SW_MUX_CTL_PAD_SAI5_RXD0                0x0148ULL
#define SW_MUX_CTL_PAD_SAI5_RXD1                0x014CULL
#define SW_MUX_CTL_PAD_SAI5_RXD2                0x0150ULL
#define SW_MUX_CTL_PAD_SAI5_RXD3                0x0154ULL
#define SW_MUX_CTL_PAD_SAI5_MCLK                0x0158ULL
#define SW_MUX_CTL_PAD_SAI1_RXFS                0x015CULL
#define SW_MUX_CTL_PAD_SAI1_RXC                 0x0160ULL
#define SW_MUX_CTL_PAD_SAI1_RXD0                0x0164ULL
#define SW_MUX_CTL_PAD_SAI1_RXD1                0x0168ULL
#define SW_MUX_CTL_PAD_SAI1_RXD2                0x016CULL
#define SW_MUX_CTL_PAD_SAI1_RXD3                0x0170ULL
#define SW_MUX_CTL_PAD_SAI1_RXD4                0x0174ULL
#define SW_MUX_CTL_PAD_SAI1_RXD5                0x0178ULL
#define SW_MUX_CTL_PAD_SAI1_RXD6                0x017CULL
#define SW_MUX_CTL_PAD_SAI1_RXD7                0x0180ULL
#define SW_MUX_CTL_PAD_SAI1_TXFS                0x0184ULL
#define SW_MUX_CTL_PAD_SAI1_TXC                 0x0188ULL
#define SW_MUX_CTL_PAD_SAI1_TXD0                0x018CULL
#define SW_MUX_CTL_PAD_SAI1_TXD1                0x0190ULL
#define SW_MUX_CTL_PAD_SAI1_TXD2                0x0194ULL
#define SW_MUX_CTL_PAD_SAI1_TXD3                0x0198ULL
#define SW_MUX_CTL_PAD_SAI1_TXD4                0x019CULL
#define SW_MUX_CTL_PAD_SAI1_TXD5                0x01A0ULL
#define SW_MUX_CTL_PAD_SAI1_TXD6                0x01A4ULL
#define SW_MUX_CTL_PAD_SAI1_TXD7                0x01A8ULL
#define SW_MUX_CTL_PAD_SAI1_MCLK                0x01ACULL
#define SW_MUX_CTL_PAD_SAI2_RXFS                0x01B0ULL
#define SW_MUX_CTL_PAD_SAI2_RXC                 0x01B4ULL
#define SW_MUX_CTL_PAD_SAI2_RXD0                0x01B8ULL
#define SW_MUX_CTL_PAD_SAI2_TXFS                0x01BCULL
#define SW_MUX_CTL_PAD_SAI2_TXC                 0x01C0ULL
#define SW_MUX_CTL_PAD_SAI2_TXD0                0x01C4ULL
#define SW_MUX_CTL_PAD_SAI2_MCLK                0x01C8ULL
#define SW_MUX_CTL_PAD_SAI3_RXFS                0x01CCULL
#define SW_MUX_CTL_PAD_SAI3_RXC                 0x01D0ULL
#define SW_MUX_CTL_PAD_SAI3_RXD                 0x01D4ULL
#define SW_MUX_CTL_PAD_SAI3_TXFS                0x01D8ULL
#define SW_MUX_CTL_PAD_SAI3_TXC                 0x01DCULL
#define SW_MUX_CTL_PAD_SAI3_TXD                 0x01E0ULL
#define SW_MUX_CTL_PAD_SAI3_MCLK                0x01E4ULL
#define SW_MUX_CTL_PAD_SPDIF_TX                 0x01E8ULL
#define SW_MUX_CTL_PAD_SPDIF_RX                 0x01ECULL
#define SW_MUX_CTL_PAD_SPDIF_EXT_CLK            0x01F0ULL
#define SW_MUX_CTL_PAD_ECSPI1_SCLK              0x01F4ULL
#define SW_MUX_CTL_PAD_ECSPI1_MOSI              0x01F8ULL
#define SW_MUX_CTL_PAD_ECSPI1_MISO              0x01FCULL
#define SW_MUX_CTL_PAD_ECSPI1_SS0               0x0200ULL
#define SW_MUX_CTL_PAD_ECSPI2_SCLK              0x0204ULL
#define SW_MUX_CTL_PAD_ECSPI2_MOSI              0x0208ULL
#define SW_MUX_CTL_PAD_ECSPI2_MISO              0x020CULL
#define SW_MUX_CTL_PAD_ECSPI2_SS0               0x0210ULL
#define SW_MUX_CTL_PAD_I2C1_SCL                 0x0214ULL
#define SW_MUX_CTL_PAD_I2C1_SDA                 0x0218ULL
#define SW_MUX_CTL_PAD_I2C2_SCL                 0x021CULL
#define SW_MUX_CTL_PAD_I2C2_SDA                 0x0220ULL
#define SW_MUX_CTL_PAD_I2C3_SCL                 0x0224ULL
#define SW_MUX_CTL_PAD_I2C3_SDA                 0x0228ULL
#define SW_MUX_CTL_PAD_I2C4_SCL                 0x022CULL
#define SW_MUX_CTL_PAD_I2C4_SDA                 0x0230ULL
#define SW_MUX_CTL_PAD_UART1_RXD                0x0234ULL
#define SW_MUX_CTL_PAD_UART1_TXD                0x0238ULL
#define SW_MUX_CTL_PAD_UART2_RXD                0x023CULL
#define SW_MUX_CTL_PAD_UART2_TXD                0x0240ULL
#define SW_MUX_CTL_PAD_UART3_RXD                0x0244ULL
#define SW_MUX_CTL_PAD_UART3_TXD                0x0248ULL
#define SW_MUX_CTL_PAD_UART4_RXD                0x024CULL
#define SW_MUX_CTL_PAD_UART4_TXD                0x0250ULL
#define SW_PAD_CTL_PAD_TEST_MODE                0x0254ULL
#define SW_PAD_CTL_PAD_BOOT_MODE0               0x0258ULL
#define SW_PAD_CTL_PAD_BOOT_MODE1               0x025CULL
#define SW_PAD_CTL_PAD_JTAG_MOD                 0x0260ULL
#define SW_PAD_CTL_PAD_JTAG_TRST_B              0x0264ULL
#define SW_PAD_CTL_PAD_JTAG_TDI                 0x0268ULL
#define SW_PAD_CTL_PAD_JTAG_TMS                 0x026CULL
#define SW_PAD_CTL_PAD_JTAG_TCK                 0x0270ULL
#define SW_PAD_CTL_PAD_JTAG_TDO                 0x0274ULL
#define SW_PAD_CTL_PAD_RTC                      0x0278ULL
#define SW_PAD_CTL_PAD_PMIC_STBY_REQ            0x027CULL
#define SW_PAD_CTL_PAD_PMIC_ON_REQ              0x0280ULL
#define SW_PAD_CTL_PAD_ONOFF                    0x0284ULL
#define SW_PAD_CTL_PAD_POR_B                    0x0288ULL
#define SW_PAD_CTL_PAD_RTC_RESET_B              0x028CULL
#define SW_PAD_CTL_PAD_GPIO1_IO00               0x0290ULL
#define SW_PAD_CTL_PAD_GPIO1_IO01               0x0294ULL
#define SW_PAD_CTL_PAD_GPIO1_IO02               0x0298ULL
#define SW_PAD_CTL_PAD_GPIO1_IO03               0x029CULL
#define SW_PAD_CTL_PAD_GPIO1_IO04               0x02A0ULL
#define SW_PAD_CTL_PAD_GPIO1_IO05               0x02A4ULL
#define SW_PAD_CTL_PAD_GPIO1_IO06               0x02A8ULL
#define SW_PAD_CTL_PAD_GPIO1_IO07               0x02ACULL
#define SW_PAD_CTL_PAD_GPIO1_IO08               0x02B0ULL
#define SW_PAD_CTL_PAD_GPIO1_IO09               0x02B4ULL
#define SW_PAD_CTL_PAD_GPIO1_IO10               0x02B8ULL
#define SW_PAD_CTL_PAD_GPIO1_IO11               0x02BCULL
#define SW_PAD_CTL_PAD_GPIO1_IO12               0x02C0ULL
#define SW_PAD_CTL_PAD_GPIO1_IO13               0x02C4ULL
#define SW_PAD_CTL_PAD_GPIO1_IO14               0x02C8ULL
#define SW_PAD_CTL_PAD_GPIO1_IO15               0x02CCULL
#define SW_PAD_CTL_PAD_ENET_MDC                 0x02D0ULL
#define SW_PAD_CTL_PAD_ENET_MDIO                0x02D4ULL
#define SW_PAD_CTL_PAD_ENET_TD3                 0x02D8ULL
#define SW_PAD_CTL_PAD_ENET_TD2                 0x02DCULL
#define SW_PAD_CTL_PAD_ENET_TD1                 0x02E0ULL
#define SW_PAD_CTL_PAD_ENET_TD0                 0x02E4ULL
#define SW_PAD_CTL_PAD_ENET_TX_CTL              0x02E8ULL
#define SW_PAD_CTL_PAD_ENET_TXC                 0x02ECULL
#define SW_PAD_CTL_PAD_ENET_RX_CTL              0x02F0ULL
#define SW_PAD_CTL_PAD_ENET_RXC                 0x02F4ULL
#define SW_PAD_CTL_PAD_ENET_RD0                 0x02F8ULL
#define SW_PAD_CTL_PAD_ENET_RD1                 0x02FCULL
#define SW_PAD_CTL_PAD_ENET_RD2                 0x0300ULL
#define SW_PAD_CTL_PAD_ENET_RD3                 0x0304ULL
#define SW_PAD_CTL_PAD_SD1_CLK                  0x0308ULL
#define SW_PAD_CTL_PAD_SD1_CMD                  0x030CULL
#define SW_PAD_CTL_PAD_SD1_DATA0                0x0310ULL
#define SW_PAD_CTL_PAD_SD1_DATA1                0x0314ULL
#define SW_PAD_CTL_PAD_SD1_DATA2                0x0318ULL
#define SW_PAD_CTL_PAD_SD1_DATA3                0x031CULL
#define SW_PAD_CTL_PAD_SD1_DATA4                0x0320ULL
#define SW_PAD_CTL_PAD_SD1_DATA5                0x0324ULL
#define SW_PAD_CTL_PAD_SD1_DATA6                0x0328ULL
#define SW_PAD_CTL_PAD_SD1_DATA7                0x032CULL
#define SW_PAD_CTL_PAD_SD1_RESET_B              0x0330ULL
#define SW_PAD_CTL_PAD_SD1_STROBE               0x0334ULL
#define SW_PAD_CTL_PAD_SD2_CD_B                 0x0338ULL
#define SW_PAD_CTL_PAD_SD2_CLK                  0x033CULL
#define SW_PAD_CTL_PAD_SD2_CMD                  0x0340ULL
#define SW_PAD_CTL_PAD_SD2_DATA0                0x0344ULL
#define SW_PAD_CTL_PAD_SD2_DATA1                0x0348ULL
#define SW_PAD_CTL_PAD_SD2_DATA2                0x034CULL
#define SW_PAD_CTL_PAD_SD2_DATA3                0x0350ULL
#define SW_PAD_CTL_PAD_SD2_RESET_B              0x0354ULL
#define SW_PAD_CTL_PAD_SD2_WP                   0x0358ULL
#define SW_PAD_CTL_PAD_NAND_ALE                 0x035CULL
#define SW_PAD_CTL_PAD_NAND_CE0_B               0x0360ULL
#define SW_PAD_CTL_PAD_NAND_CE1_B               0x0364ULL
#define SW_PAD_CTL_PAD_NAND_CE2_B               0x0368ULL
#define SW_PAD_CTL_PAD_NAND_CE3_B               0x036CULL
#define SW_PAD_CTL_PAD_NAND_CLE                 0x0370ULL
#define SW_PAD_CTL_PAD_NAND_DATA00              0x0374ULL
#define SW_PAD_CTL_PAD_NAND_DATA01              0x0378ULL
#define SW_PAD_CTL_PAD_NAND_DATA02              0x037CULL
#define SW_PAD_CTL_PAD_NAND_DATA03              0x0380ULL
#define SW_PAD_CTL_PAD_NAND_DATA04              0x0384ULL
#define SW_PAD_CTL_PAD_NAND_DATA05              0x0388ULL
#define SW_PAD_CTL_PAD_NAND_DATA06              0x038CULL
#define SW_PAD_CTL_PAD_NAND_DATA07              0x0390ULL
#define SW_PAD_CTL_PAD_NAND_DQS                 0x0394ULL
#define SW_PAD_CTL_PAD_NAND_RE_B                0x0398ULL
#define SW_PAD_CTL_PAD_NAND_READY_B             0x039CULL
#define SW_PAD_CTL_PAD_NAND_WE_B                0x03A0ULL
#define SW_PAD_CTL_PAD_NAND_WP_B                0x03A4ULL
#define SW_PAD_CTL_PAD_SAI5_RXFS                0x03A8ULL
#define SW_PAD_CTL_PAD_SAI5_RXC                 0x03ACULL
#define SW_PAD_CTL_PAD_SAI5_RXD0                0x03B0ULL
#define SW_PAD_CTL_PAD_SAI5_RXD1                0x03B4ULL
#define SW_PAD_CTL_PAD_SAI5_RXD2                0x03B8ULL
#define SW_PAD_CTL_PAD_SAI5_RXD3                0x03BCULL
#define SW_PAD_CTL_PAD_SAI5_MCLK                0x03C0ULL
#define SW_PAD_CTL_PAD_SAI1_RXFS                0x03C4ULL
#define SW_PAD_CTL_PAD_SAI1_RXC                 0x03C8ULL
#define SW_PAD_CTL_PAD_SAI1_RXD0                0x03CCULL
#define SW_PAD_CTL_PAD_SAI1_RXD1                0x03D0ULL
#define SW_PAD_CTL_PAD_SAI1_RXD2                0x03D4ULL
#define SW_PAD_CTL_PAD_SAI1_RXD3                0x03D8ULL
#define SW_PAD_CTL_PAD_SAI1_RXD4                0x03DCULL
#define SW_PAD_CTL_PAD_SAI1_RXD5                0x03E0ULL
#define SW_PAD_CTL_PAD_SAI1_RXD6                0x03E4ULL
#define SW_PAD_CTL_PAD_SAI1_RXD7                0x03E8ULL
#define SW_PAD_CTL_PAD_SAI1_TXFS                0x03ECULL
#define SW_PAD_CTL_PAD_SAI1_TXC                 0x03F0ULL
#define SW_PAD_CTL_PAD_SAI1_TXD0                0x03F4ULL
#define SW_PAD_CTL_PAD_SAI1_TXD1                0x03F8ULL
#define SW_PAD_CTL_PAD_SAI1_TXD2                0x03FCULL
#define SW_PAD_CTL_PAD_SAI1_TXD3                0x0400ULL
#define SW_PAD_CTL_PAD_SAI1_TXD4                0x0404ULL
#define SW_PAD_CTL_PAD_SAI1_TXD5                0x0408ULL
#define SW_PAD_CTL_PAD_SAI1_TXD6                0x040CULL
#define SW_PAD_CTL_PAD_SAI1_TXD7                0x0410ULL
#define SW_PAD_CTL_PAD_SAI1_MCLK                0x0414ULL
#define SW_PAD_CTL_PAD_SAI2_RXFS                0x0418ULL
#define SW_PAD_CTL_PAD_SAI2_RXC                 0x041CULL
#define SW_PAD_CTL_PAD_SAI2_RXD0                0x0420ULL
#define SW_PAD_CTL_PAD_SAI2_TXFS                0x0424ULL
#define SW_PAD_CTL_PAD_SAI2_TXC                 0x0428ULL
#define SW_PAD_CTL_PAD_SAI2_TXD0                0x042CULL
#define SW_PAD_CTL_PAD_SAI2_MCLK                0x0430ULL
#define SW_PAD_CTL_PAD_SAI3_RXFS                0x0434ULL
#define SW_PAD_CTL_PAD_SAI3_RXC                 0x0438ULL
#define SW_PAD_CTL_PAD_SAI3_RXD                 0x043CULL
#define SW_PAD_CTL_PAD_SAI3_TXFS                0x0440ULL
#define SW_PAD_CTL_PAD_SAI3_TXC                 0x0444ULL
#define SW_PAD_CTL_PAD_SAI3_TXD                 0x0448ULL
#define SW_PAD_CTL_PAD_SAI3_MCLK                0x044CULL
#define SW_PAD_CTL_PAD_SPDIF_TX                 0x0450ULL
#define SW_PAD_CTL_PAD_SPDIF_RX                 0x0454ULL
#define SW_PAD_CTL_PAD_SPDIF_EXT_CLK            0x0458ULL
#define SW_PAD_CTL_PAD_ECSPI1_SCLK              0x045CULL
#define SW_PAD_CTL_PAD_ECSPI1_MOSI              0x0460ULL
#define SW_PAD_CTL_PAD_ECSPI1_MISO              0x0464ULL
#define SW_PAD_CTL_PAD_ECSPI1_SS0               0x0468ULL
#define SW_PAD_CTL_PAD_ECSPI2_SCLK              0x046CULL
#define SW_PAD_CTL_PAD_ECSPI2_MOSI              0x0470ULL
#define SW_PAD_CTL_PAD_ECSPI2_MISO              0x0474ULL
#define SW_PAD_CTL_PAD_ECSPI2_SS0               0x0478ULL
#define SW_PAD_CTL_PAD_I2C1_SCL                 0x047CULL
#define SW_PAD_CTL_PAD_I2C1_SDA                 0x0480ULL
#define SW_PAD_CTL_PAD_I2C2_SCL                 0x0484ULL
#define SW_PAD_CTL_PAD_I2C2_SDA                 0x0488ULL
#define SW_PAD_CTL_PAD_I2C3_SCL                 0x048CULL
#define SW_PAD_CTL_PAD_I2C3_SDA                 0x0490ULL
#define SW_PAD_CTL_PAD_I2C4_SCL                 0x0494ULL
#define SW_PAD_CTL_PAD_I2C4_SDA                 0x0498ULL
#define SW_PAD_CTL_PAD_UART1_RXD                0x049CULL
#define SW_PAD_CTL_PAD_UART1_TXD                0x04A0ULL
#define SW_PAD_CTL_PAD_UART2_RXD                0x04A4ULL
#define SW_PAD_CTL_PAD_UART2_TXD                0x04A8ULL
#define SW_PAD_CTL_PAD_UART3_RXD                0x04ACULL
#define SW_PAD_CTL_PAD_UART3_TXD                0x04B0ULL
#define SW_PAD_CTL_PAD_UART4_RXD                0x04B4ULL
#define SW_PAD_CTL_PAD_UART4_TXD                0x04B8ULL
#define CCM_PMIC_READY_SELECT_INPUT             0x04BCULL
#define ENET1_MDIO_SELECT_INPUT                 0x04C0ULL
#define SAI1_RX_SYNC_SELECT_INPUT               0x04C4ULL
#define SAI1_TX_BCLK_SELECT_INPUT               0x04C8ULL
#define SAI1_TX_SYNC_SELECT_INPUT               0x04CCULL
#define SAI5_RX_BCLK_SELECT_INPUT               0x04D0ULL
#define SAI5_RXD0_SELECT_INPUT                  0x04D4ULL
#define SAI5_RXD1_SELECT_INPUT                  0x04D8ULL
#define SAI5_RXD2_SELECT_INPUT                  0x04DCULL
#define SAI5_RXD3_SELECT_INPUT                  0x04E0ULL
#define SAI5_RX_SYNC_SELECT_INPUT               0x04E4ULL
#define SAI5_TX_BCLK_SELECT_INPUT               0x04E8ULL
#define SAI5_TX_SYNC_SELECT_INPUT               0x04ECULL
#define UART1_RTS_B_SELECT_INPUT                0x04F0ULL
#define UART1_RXD_SELECT_INPUT                  0x04F4ULL
#define UART2_RTS_B_SELECT_INPUT                0x04F8ULL
#define UART2_RXD_SELECT_INPUT                  0x04FCULL
#define UART3_RTS_B_SELECT_INPUT                0x0500ULL
#define UART3_RXD_SELECT_INPUT                  0x0504ULL
#define UART4_RTS_B_SELECT_INPUT                0x0508ULL
#define UART4_RXD_SELECT_INPUT                  0x050CULL
#define SAI6_RX_BCLK_SELECT_INPUT               0x0510ULL
#define SAI6_RXD0_SELECT_INPUT                  0x0514ULL
#define SAI6_RX_SYNC_SELECT_INPUT               0x0518ULL
#define SAI6_TX_BCLK_SELECT_INPUT               0x051CULL
#define SAI6_TX_SYNC_SELECT_INPUT               0x0520ULL
#define PCIE1_CLKREQ_B_SELECT_INPUT             0x0524ULL
#define PCIE2_CLKREQ_B_SELECT_INPUT             0x0528ULL
#define SAI5_MCLK_SELECT_INPUT                  0x052CULL
#define SAI6_MCLK_SELECT_INPUT                  0x0530ULL

/* MUX CTRL Register Bit Defs */
#define IOMUX_CFG_MUX_MODE_START        0
#define IOMUX_CFG_MUX_MODE_COUNT        3
#define IOMUX_CFG_MUX_MODE_VAL(x)   \
                        ((x & ((1 << IOMUX_CFG_MUX_MODE_COUNT) - 1)) << IOMUX_CFG_MUX_MODE_START)

#define IOMUX_CFG_SION_START            4
#define IOMUX_CFG_SION_COUNT            1
#define IOMUX_CFG_SION_VAL(x)   \
                        ((x & ((1 << IOMUX_CFG_SION_COUNT) - 1)) << IOMUX_CFG_SION_START)

/* PAD CTRL Bit Defs */
#define IOMUX_CFG_DSE_START             0
#define IOMUX_CFG_DSE_COUNT             3
#define IOMUX_CFG_DSE_VAL(x)    \
                        ((x & ((1 << IOMUX_CFG_DSE_COUNT) - 1)) << IOMUX_CFG_DSE_START)

#define IOMUX_CFG_SRE_START             3
#define IOMUX_CFG_SRE_COUNT             2
#define IOMUX_CFG_SRE_VAL(x)    \
                        ((x & ((1 << IOMUX_CFG_SRE_COUNT) - 1)) << IOMUX_CFG_SRE_START)

#define IOMUX_CFG_ODE_START             5
#define IOMUX_CFG_ODE_COUNT             1
#define IOMUX_CFG_ODE_VAL(x)    \
                        ((x & ((1 << IOMUX_CFG_ODE_COUNT) - 1)) << IOMUX_CFG_ODE_START)

#define IOMUX_CFG_PUE_START             6
#define IOMUX_CFG_PUE_COUNT             1
#define IOMUX_CFG_PUE_VAL(x)    \
                        ((x & ((1 << IOMUX_CFG_PUE_COUNT) - 1)) << IOMUX_CFG_PUE_START)

#define IOMUX_CFG_HYS_START             7
#define IOMUX_CFG_HYS_COUNT             1
#define IOMUX_CFG_HYS_VAL(x)    \
                        ((x & ((1 << IOMUX_CFG_HYS_COUNT) - 1)) << IOMUX_CFG_HYS_START)

#define IOMUX_CFG_LVTTL_START           8
#define IOMUX_CFG_LVTTL_COUNT           1
#define IOMUX_CFG_LVTTL_VAL(x)  \
                        ((x & ((1 << IOMUX_CFG_LVTTL_COUNT) - 1)) << IOMUX_CFG_LVTTL_START)

#define IOMUX_CFG_VSEL_START            11
#define IOMUX_CFG_VSEL_COUNT            3
#define IOMUX_CFG_VSEL_VAL(x)   \
                        ((x & ((1 << IOMUX_CFG_VSEL_COUNT) - 1)) << IOMUX_CFG_VSEL_START)

#define IOMUX_CFG_DAISY_START           0
#define IOMUX_CFG_DAISY_COUNT           1
#define IOMUX_CFG_DAISY_VAL(x)  \
                        ((x & ((1 << IOMUX_CFG_DAISY_COUNT) - 1)) << IOMUX_CFG_DAISY_START)


#define DSE_HIZ                 (0x00)
#define DSR_255_OHM             (0x01)
#define DSR_105_OHM             (0x02)
#define DSR_75_OHM              (0x03)
#define DSR_85_OHM              (0x04)
#define DSR_65_OHM              (0x05)
#define DSR_45_OHM              (0x06)
#define DSR_40_OHM              (0x07)

#define SRE_SLOW                (0x00)
#define SRE_MEDIUM              (0x01)
#define SRE_FAST                (0x02)
#define SRE_MAX                 (0x03)

#define VSEL_0_AUTO             (0x00)
#define VSEL_1_AUTO             (0x01)
#define VSEL_2_AUTO             (0x02)
#define VSEL_3_AUTO             (0x03)
#define VSEL_4_MAN_3V3          (0x04)
#define VSEL_5_MAN_2P5          (0x05)
#define VSEL_6_MAN_2p5          (0x06)
#define VSEL_7_MAN_1P2_1P8      (0x07)



zx_status_t imx8m_config_pin(imx8m_t *dev, iomux_cfg_struct* s_cfg, int size);