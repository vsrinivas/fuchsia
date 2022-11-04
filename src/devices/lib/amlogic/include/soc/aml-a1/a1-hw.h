// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HW_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HW_H_

// clock control registers
#define A1_CLK_BASE 0xfe000800
#define A1_CLK_LENGTH 0x400

#define A1_MSR_CLK_BASE 0xfe003400
#define A1_MSR_CLK_LENGTH 0x400

// Analog Control for PLL clock
#define A1_ANACTRL_BASE 0xfe007c00
#define A1_ANACTRL_LENGTH 0x400

#define A1_ANACTRL_SYSPLL_CTRL0 (0x0040 << 2)  // 0x100
#define A1_ANACTRL_SYSPLL_CTRL1 (0x0041 << 2)  // 0x104
#define A1_ANACTRL_SYSPLL_CTRL2 (0x0042 << 2)  // 0x108
#define A1_ANACTRL_SYSPLL_CTRL3 (0x0043 << 2)  // 0x10c
#define A1_ANACTRL_SYSPLL_CTRL4 (0x0044 << 2)  // 0x110
#define A1_ANACTRL_SYSPLL_STS (0x0045 << 2)    // 0x114

#define A1_ANACTRL_HIFIPLL_CTRL0 (0x0050 << 2)  // 0x140
#define A1_ANACTRL_HIFIPLL_CTRL1 (0x0051 << 2)  // 0x144
#define A1_ANACTRL_HIFIPLL_CTRL2 (0x0052 << 2)  // 0x148
#define A1_ANACTRL_HIFIPLL_CTRL3 (0x0053 << 2)  // 0x14c
#define A1_ANACTRL_HIFIPLL_CTRL4 (0x0054 << 2)  // 0x150
#define A1_ANACTRL_HIFIPLL_STS (0x0055 << 2)    // 0x154

// gpio
#define A1_GPIO_BASE 0xfe000400
#define A1_GPIO_LENGTH 0x400
#define A1_GPIO_INTERRUPT_BASE ((0x10 << 2) + A1_GPIO_BASE)
#define A1_GPIO_INTERRUPT_LENGTH 0x14

// i2c
#define A1_I2C_LENGTH 0x200
#define A1_I2C_A_BASE 0xfe001400
#define A1_I2C_B_BASE 0xfe005c00
#define A1_I2C_C_BASE 0xfe006800
#define A1_I2C_D_BASE 0xfe006c00

// spicc
#define A1_SPICC0_BASE 0xfe003800
#define A1_SPICC0_LENGTH 0x400

// rtc

// mailbox

// dsp

// Peripherals - datasheet is nondescript about this section, but it contains
//  top level ethernet control and temp sensor registers

// DMC
#define A1_DMC_BASE 0xfd020000
#define A1_DMC_LENGTH 0x400

// NNA

// Power domain

// Memory Power Domain

// Reset

// IRQs
#define A1_I2C_A_IRQ 64    // 32+32
#define A1_SPICC0_IRQ 80   // 32+48
#define A1_GPIO_IRQ_0 81   // 32+49
#define A1_GPIO_IRQ_1 82   // 32+50
#define A1_GPIO_IRQ_2 83   // 32+51
#define A1_GPIO_IRQ_3 84   // 32+52
#define A1_GPIO_IRQ_4 85   // 32+53
#define A1_GPIO_IRQ_5 86   // 32+54
#define A1_GPIO_IRQ_6 87   // 32+55
#define A1_GPIO_IRQ_7 88   // 32+56
#define A1_TS_PLL_IRQ 89   // 57+32
#define A1_I2C_B_IRQ 100   // 32+68
#define A1_I2C_C_IRQ 108   // 32+76
#define A1_I2C_D_IRQ 110   // 32+78
#define A1_DDR_BW_IRQ 141  // 109+32

// PWM

// AUDIO

// For 'fdf::MmioBuffer::Create'
// |base| is guaranteed to be page aligned.

// USB

// sys_ctrl

// sticky register -  not reset by watchdog
#define A1_SYS_CTRL_SEC_STATUS_REG13 ((0x00cd << 2) + 0xfe005800)

// Temperature
#define A1_TEMP_SENSOR_PLL_BASE 0xfe004c00
#define A1_TEMP_SENSOR_PLL_LENGTH 0x50
#define A1_TEMP_SENSOR_PLL_TRIM A1_SYS_CTRL_SEC_STATUS_REG13
#define A1_TEMP_SENSOR_PLL_TRIM_LENGTH 0x4

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HW_H_
