// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define S905D2_GPIO_BASE                0xff634400
#define S905D2_GPIO_LENGTH              0x400
#define S905D2_GPIO_A0_BASE             0xff800000
#define S905D2_GPIO_AO_LENGTH           0x1000
#define S905D2_GPIO_INTERRUPT_BASE      0xffd00000
#define S905D2_GPIO_INTERRUPT_LENGTH    0x10000

#define S905D2_USB0_BASE                0xff500000
#define S905D2_USB0_LENGTH              0x100000

#define S905D2_USBCTRL_BASE             0xffe09000
#define S905D2_USBCTRL_LENGTH           0x2000

#define S905D2_RESET_BASE               0xffd01000
#define S905D2_RESET_LENGTH             0x1000

#define S905D2_USBPHY20_BASE            0xff636000
#define S905D2_USBPHY20_LENGTH          0x2000

#define S905D2_USBPHY21_BASE            0xff63a000
#define S905D2_USBPHY21_LENGTH          0x2000

// Reset register offsets
#define S905D2_RESET0_REGISTER          0x04
#define S905D2_RESET1_REGISTER          0x08
#define S905D2_RESET1_USB               (1 << 2)    // bit to reset USB
#define S905D2_RESET2_REGISTER          0x0c
#define S905D2_RESET3_REGISTER          0x10
#define S905D2_RESET4_REGISTER          0x14
#define S905D2_RESET6_REGISTER          0x1c
#define S905D2_RESET7_REGISTER          0x20
#define S905D2_RESET0_MASK              0x40
#define S905D2_RESET1_MASK              0x44
#define S905D2_RESET2_MASK              0x48
#define S905D2_RESET3_MASK              0x4c
#define S905D2_RESET4_MASK              0x50
#define S905D2_RESET6_MASK              0x58
#define S905D2_RESET7_MASK              0x5c
#define S905D2_RESET0_LEVEL             0x80
#define S905D2_RESET1_LEVEL             0x84
#define S905D2_RESET2_LEVEL             0x88
#define S905D2_RESET3_LEVEL             0x8c
#define S905D2_RESET4_LEVEL             0x90
#define S905D2_RESET6_LEVEL             0x98
#define S905D2_RESET7_LEVEL             0x9c

#define S905D2_USB0_IRQ                 62
#define S905D2_GPIO_IRQ_0               94
#define S905D2_GPIO_IRQ_1               95
#define S905D2_GPIO_IRQ_2               96
#define S905D2_GPIO_IRQ_3               97
#define S905D2_GPIO_IRQ_4               98
#define S905D2_GPIO_IRQ_5               99
#define S905D2_GPIO_IRQ_6               100
#define S905D2_GPIO_IRQ_7               101
#define S905D2_A0_GPIO_IRQ_0            238
#define S905D2_A0_GPIO_IRQ_1            239
