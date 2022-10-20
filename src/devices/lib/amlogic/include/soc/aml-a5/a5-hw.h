// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_

// clock control registers
#define A5_CLK_BASE 0xfe000000
#define A5_CLK_LENGTH 0x2000

#define A5_MSR_CLK_BASE 0xfe048000
#define A5_MSR_CLK_LENGTH 0x2000
#define A5_MSR_CLK_REG0 (0x0000 << 2)
#define A5_MSR_CLK_REG2 (0x0002 << 2)

// gpio
#define A5_GPIO_BASE 0xfe004000
#define A5_GPIO_LENGTH 0x2000
#define A5_GPIO_INTERRUPT_BASE ((0x0020 << 2) + A5_GPIO_BASE)
#define A5_GPIO_INTERRUPT_LENGTH 0x8

// i2c
#define A5_I2C_LENGTH 0x2000
#define A5_I2C_A_BASE 0xfe066000
#define A5_I2C_B_BASE 0xfe068000
#define A5_I2C_C_BASE 0xfe06a000
#define A5_I2C_D_BASE 0xfe06c000

// spicc
#define A5_SPICC0_BASE 0xfe050000
#define A5_SPICC1_BASE 0xfe052000
#define A5_SPICC0_LENGTH 0x1000
#define A5_SPICC1_LENGTH 0x1000

// rtc
#define A5_RTC_BASE 0xfe09a000
#define A5_RTC_LENGTH 0x2000

// mailbox
#define A5_MAILBOX_WR_BASE 0xfe006000
#define A5_MAILBOX_WR_LENGTH 0x800
#define A5_MAILBOX_RD_BASE 0xfe006800
#define A5_MAILBOX_RD_LENGTH 0x800
#define A5_MAILBOX_SET_BASE 0xfe0070c0
#define A5_MAILBOX_SET_LENGTH 0x40
#define A5_MAILBOX_CLR_BASE 0xfe007100
#define A5_MAILBOX_CLR_LENGTH 0x40
#define A5_MAILBOX_STS_BASE 0xfe007140
#define A5_MAILBOX_STS_LENGTH 0x40
#define A5_MAILBOX_IRQCTRL_BASE 0xfe007020
#define A5_MAILBOX_IRQCTRL_LENGTH 0x40

// dsp
#define A5_DSPA_BASE 0xfe340018
#define A5_DSPA_BASE_LENGTH 0x114
#define A5_DSP_SRAM_BASE 0xf7030000
#define A5_DSP_SRAM_BASE_LENGTH 0x1d0000

// Peripherals - datasheet is nondescript about this section, but it contains
//  top level ethernet control and temp sensor registers

// Analog Control
#define PLL_LOCK_CHECK_MAX 3
#define A5_ANACTRL_BASE 0xfe008000
#define A5_ANACTRL_LENGTH 0x1000

#define A5_ANACTRL_SYSPLL_CTRL0 (0x0000 << 2)
#define A5_ANACTRL_SYSPLL_CTRL1 (0x0001 << 2)
#define A5_ANACTRL_SYSPLL_CTRL2 (0x0002 << 2)
#define A5_ANACTRL_SYSPLL_CTRL3 (0x0003 << 2)
#define A5_ANACTRL_SYSPLL_CTRL4 (0x0004 << 2)
#define A5_ANACTRL_SYSPLL_CTRL5 (0x0005 << 2)
#define A5_ANACTRL_SYSPLL_CTRL6 (0x0006 << 2)
#define A5_ANACTRL_SYSPLL_STS (0x0007 << 2)

#define A5_ANACTRL_HIFIPLL_CTRL0 (0x0040 << 2)  // 0x100
#define A5_ANACTRL_HIFIPLL_CTRL1 (0x0041 << 2)  // 0x104
#define A5_ANACTRL_HIFIPLL_CTRL2 (0x0042 << 2)  // 0x108
#define A5_ANACTRL_HIFIPLL_CTRL3 (0x0043 << 2)  // 0x10c
#define A5_ANACTRL_HIFIPLL_CTRL4 (0x0044 << 2)  // 0x110
#define A5_ANACTRL_HIFIPLL_CTRL5 (0x0045 << 2)  // 0x114
#define A5_ANACTRL_HIFIPLL_CTRL6 (0x0046 << 2)  // 0x118
#define A5_ANACTRL_HIFIPLL_STS (0x0047 << 2)    // 0x11c

#define A5_ANACTRL_MPLL_CTRL1 (0x0061 << 2)
#define A5_ANACTRL_MPLL_CTRL2 (0x0062 << 2)
#define A5_ANACTRL_MPLL_CTRL3 (0x0063 << 2)
#define A5_ANACTRL_MPLL_CTRL4 (0x0064 << 2)
#define A5_ANACTRL_MPLL_CTRL5 (0x0065 << 2)
#define A5_ANACTRL_MPLL_CTRL6 (0x0066 << 2)
#define A5_ANACTRL_MPLL_CTRL7 (0x0067 << 2)
#define A5_ANACTRL_MPLL_CTRL8 (0x0068 << 2)
// Ethernet

// eMMC
#define A5_EMMC_A_BASE 0xfe088000
#define A5_EMMC_A_LENGTH 0x2000
#define A5_EMMC_B_BASE 0xfe08a000
#define A5_EMMC_B_LENGTH 0x2000
#define A5_EMMC_C_BASE 0xfe08c000
#define A5_EMMC_C_LENGTH 0x2000

// DMC
#define A5_DMC_BASE 0xfe036000
#define A5_DMC_LENGTH 0x400
// NNA
#define A5_NNA_BASE 0xfdb00000
#define A5_NNA_LENGTH 0x40000

#define A5_NNA_SRAM_BASE 0xf7000000
#define A5_NNA_SRAM_LENGTH 0x200000

// Power domain
#define A5_POWER_DOMAIN_BASE 0xfe00c000
#define A5_POWER_DOMAIN_LENGTH 0x400

// Memory Power Domain
#define A5_MEMORY_PD_BASE 0xfe00c000
#define A5_MEMORY_PD_LENGTH 0x400

// Reset
#define A5_RESET_BASE 0xfe002000
#define A5_RESET_LENGTH 0x2000

#define A5_RESET0_REGISTER 0x0
#define A5_RESET1_REGISTER 0x4
#define A5_RESET2_REGISTER 0x8
#define A5_RESET3_REGISTER 0xc
#define A5_RESET4_REGISTER 0x10
#define A5_RESET5_REGISTER 0x14
#define A5_RESET0_LEVEL 0x40
#define A5_RESET1_LEVEL 0x44
#define A5_RESET2_LEVEL 0x48
#define A5_RESET3_LEVEL 0x4c
#define A5_RESET4_LEVEL 0x50
#define A5_RESET5_LEVEL 0x54
#define A5_RESET0_MASK 0x80
#define A5_RESET1_MASK 0x84
#define A5_RESET2_MASK 0x88
#define A5_RESET3_MASK 0x8c
#define A5_RESET4_MASK 0x90
#define A5_RESET5_MASK 0x94

// IRQs
#define A5_GPIO_IRQ_0 42   // 32+10
#define A5_GPIO_IRQ_1 43   // 32+11
#define A5_GPIO_IRQ_2 44   // 32+12
#define A5_GPIO_IRQ_3 45   // 32+13
#define A5_GPIO_IRQ_4 46   // 32+14
#define A5_GPIO_IRQ_5 47   // 32+15
#define A5_GPIO_IRQ_6 48   // 32+16
#define A5_GPIO_IRQ_7 49   // 32+17
#define A5_GPIO_IRQ_8 50   // 32+18
#define A5_GPIO_IRQ_9 51   // 32+19
#define A5_GPIO_IRQ_10 52  // 32+20
#define A5_GPIO_IRQ_11 53  // 32+21

#define A5_TS_PLL_IRQ 62     // 30+32
#define A5_AUDIO_TODDR_A 64  // 32+32
#define A5_AUDIO_TODDR_B 65  // 33+32
#define A5_AUDIO_SPDIFIN 67  // 35+32
#define A5_AUDIO_FRDDR_A 68  // 36+32
#define A5_AUDIO_FRDDR_B 69  // 37+32
#define A5_AUDIO_FRDDR_C 70  // 38+32

#define A5_DDR_BW_IRQ 110  // 78+32

#define A5_NNA_IRQ 160        // 128+32
#define A5_USB_IDDIG_IRQ 161  // 129+32
#define A5_USB2DRD_IRQ 162    // 130+32

#define A5_I2C_A_IRQ 192  // 160+32
#define A5_I2C_B_IRQ 193  // 161+32
#define A5_I2C_C_IRQ 194  // 162+32
#define A5_I2C_D_IRQ 195  // 163+32

#define A5_SD_EMMC_A_IRQ 208  // 176+32
#define A5_SD_EMMC_B_IRQ 209  // 177+32
#define A5_SD_EMMC_C_IRQ 210  // 178+32

#define A5_SPICC0_IRQ 215  // 183+32
#define A5_SPICC1_IRQ 216  // 184+32

#define A5_ETH_GMAC_IRQ 106  // 74+32

#define A5_RTC_IRQ 163  // 131+32

#define A5_MAILBOX_IRQ 280  // 248+32

// Ethernet
#define A5_ETH_MAC_BASE 0xfdc00000
#define A5_ETH_MAC_LENGTH 0x10000

// PWM
#define A5_PWM_LENGTH 0x2000  // applies to each PWM bank
#define A5_PWM_AB_BASE 0xfe058000
#define A5_PWM_PWM_A 0x0
#define A5_PWM_PWM_B 0x4
#define A5_PWM_MISC_REG_AB 0x8
#define A5_DS_A_B 0xc
#define A5_PWM_TIME_AB 0x10
#define A5_PWM_A2 0x14
#define A5_PWM_B2 0x18
#define A5_PWM_BLINK_AB 0x1c
#define A5_PWM_LOCK_AB 0x20

#define A5_PWM_CD_BASE 0xfe05a000
#define A5_PWM_PWM_C 0x0
#define A5_PWM_PWM_D 0x4
#define A5_PWM_MISC_REG_CD 0x8
#define A5_DS_C_D 0xc
#define A5_PWM_TIME_CD 0x10
#define A5_PWM_C2 0x14
#define A5_PWM_D2 0x18
#define A5_PWM_BLINK_CD 0x1c
#define A5_PWM_LOCK_CD 0x20

#define A5_PWM_EF_BASE 0xfe05c000
#define A5_PWM_PWM_E 0x0
#define A5_PWM_PWM_F 0x4
#define A5_PWM_MISC_REG_EF 0x8
#define A5_DS_E_F 0xc
#define A5_PWM_TIME_EF 0x10
#define A5_PWM_E2 0x14
#define A5_PWM_F2 0x18
#define A5_PWM_BLINK_EF 0x1c
#define A5_PWM_LOCK_EF 0x20

#define A5_PWM_GH_BASE 0xfe05e000
#define A5_PWM_PWM_G 0x0
#define A5_PWM_PWM_H 0x4
#define A5_PWM_MISC_REG_GH 0x8
#define A5_DS_G_H 0xc
#define A5_PWM_TIME_GH 0x10
#define A5_PWM_G2 0x14
#define A5_PWM_H2 0x18
#define A5_PWM_BLINK_GH 0x1c
#define A5_PWM_LOCK_GH 0x20

// AUDIO
#define A5_EE_AUDIO_BASE 0xfe330000
#define A5_EE_AUDIO_LENGTH 0x1000

#define A5_EE_PDM_BASE 0xfe331000
#define A5_EE_PDM_LENGTH 0x100

#define A5_EE_PDM_B_BASE 0xfe334800
#define A5_EE_PDM_B_LENGTH 0x100

#define A5_EE_AUDIO2_BASE 0xfe334c00
#define A5_EE_AUDIO2_LENGTH 0x300

// For 'fdf::MmioBuffer::Create'
// |base| is guaranteed to be page aligned.
#define A5_EE_AUDIO2_BASE_ALIGN 0xfe334000
#define A5_EE_AUDIO2_LENGTH_ALIGN 0x1000
#define A5_EE_AUDIO2_CLK_GATE_EN0 (0xc00 + (0x0003 << 2))

// USB
#define A5_USB_BASE 0xfdd00000
#define A5_USB_LENGTH 0x100000

#define A5_USBCOMB_BASE 0xfe03a000
#define A5_USBCOMB_LENGTH 0x2000

#define A5_USBPHY_BASE 0xfe03c000
#define A5_USBPHY_LENGTH 0x2000

// sys_ctrl
#define A5_SYS_CTRL_BASE 0xfe010000
#define A5_SYS_CTRL_LENGTH 0x2000

// sticky register -  not reset by watchdog
#define A5_SYS_CTRL_STICKY_REG0 ((0x00b0 << 2) + 0xfe010000)
#define A5_SYS_CTRL_STICKY_REG1 ((0x00b1 << 2) + 0xfe010000)
#define A5_SYS_CTRL_STICKY_REG2 ((0x00b2 << 2) + 0xfe010000)  // AO_CPU - rtc
#define A5_SYS_CTRL_STICKY_REG3 ((0x00b3 << 2) + 0xfe010000)  // AO_CPU - tsensor
#define A5_SYS_CTRL_STICKY_REG4 ((0x00b4 << 2) + 0xfe010000)
#define A5_SYS_CTRL_STICKY_REG5 ((0x00b5 << 2) + 0xfe010000)
#define A5_SYS_CTRL_STICKY_REG6 ((0x00b6 << 2) + 0xfe010000)
#define A5_SYS_CTRL_STICKY_REG7 ((0x00b7 << 2) + 0xfe010000)      // AO_CPU - wakeup reason
#define A5_SYS_CTRL_SEC_STATUS_REG0 ((0x00c0 << 2) + 0xfe010000)  // cpu version

// Temperature
#define A5_TEMP_SENSOR_PLL_BASE 0xfe020000
#define A5_TEMP_SENSOR_PLL_LENGTH 0x50
#define A5_TEMP_SENSOR_PLL_TRIM A5_SYS_CTRL_STICKY_REG3
#define A5_TEMP_SENSOR_PLL_TRIM_LENGTH 0x4

// These registers are used to derive calibration data for the temperature sensors. The registers
// are not documented in the datasheet - they were copied over from u-boot/Cast code.

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_
