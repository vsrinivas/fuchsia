// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HW_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HW_H_

// clock control registers

// gpio
#define A1_GPIO_BASE 0xfe000400
#define A1_GPIO_LENGTH 0x400
#define A1_GPIO_INTERRUPT_BASE ((0x10 << 2) + A1_GPIO_BASE)
#define A1_GPIO_INTERRUPT_LENGTH 0x14

// i2c

// spicc

// rtc

// mailbox

// dsp

// Peripherals - datasheet is nondescript about this section, but it contains
//  top level ethernet control and temp sensor registers

// Analog Control

// DMC

// NNA

// Power domain

// Memory Power Domain

// Reset

// IRQs
#define A1_GPIO_IRQ_0 81  // 32+49
#define A1_GPIO_IRQ_1 82  // 32+50
#define A1_GPIO_IRQ_2 83  // 32+51
#define A1_GPIO_IRQ_3 84  // 32+52
#define A1_GPIO_IRQ_4 85  // 32+53
#define A1_GPIO_IRQ_5 86  // 32+54
#define A1_GPIO_IRQ_6 87  // 32+55
#define A1_GPIO_IRQ_7 88  // 32+56

// PWM

// AUDIO

// For 'fdf::MmioBuffer::Create'
// |base| is guaranteed to be page aligned.

// USB

// sys_ctrl

// sticky register -  not reset by watchdog

// Temperature

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HW_H_
