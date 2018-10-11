// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define T931_GPIO_BASE                  0xff634400
#define T931_GPIO_LENGTH                0x400
#define T931_GPIO_A0_BASE               0xff800000
#define T931_GPIO_AO_LENGTH             0x1000
#define T931_GPIO_INTERRUPT_BASE        0xffd00000
#define T931_GPIO_INTERRUPT_LENGTH      0x10000
#define T931_I2C_AOBUS_BASE             (T931_GPIO_A0_BASE + 0x5000)
#define T931_CBUS_BASE                  0xffd00000
#define T931_CBUS_LENGTH                0x100000
#define T931_I2C0_BASE                  (T931_CBUS_BASE + 0x1f000)
#define T931_I2C1_BASE                  (T931_CBUS_BASE + 0x1e000)
#define T931_I2C2_BASE                  (T931_CBUS_BASE + 0x1d000)
#define T931_I2C3_BASE                  (T931_CBUS_BASE + 0x1c000)

#define T931_HIU_BASE                   0xff63c000
#define T931_HIU_LENGTH                 0x2000

#define T931_MSR_CLK_BASE               0xffd18000
#define T931_MSR_CLK_LENGTH             0x1000

#define T931_USB0_BASE                  0xff500000
#define T931_USB0_LENGTH                0x100000

#define T931_USBPHY21_BASE              0xff63a000
#define T931_USBPHY21_LENGTH            0x2000

// IRQs
#define T931_GPIO_IRQ_0                 96
#define T931_GPIO_IRQ_1                 97
#define T931_GPIO_IRQ_2                 98
#define T931_GPIO_IRQ_3                 99
#define T931_GPIO_IRQ_4                 100
#define T931_GPIO_IRQ_5                 101
#define T931_GPIO_IRQ_6                 102
#define T931_GPIO_IRQ_7                 103
#define T931_I2C0_IRQ                   53
#define T931_I2C1_IRQ                   246
#define T931_I2C2_IRQ                   247
#define T931_I2C3_IRQ                   71
#define T931_I2C_AO_0_IRQ               227
#define T931_USB0_IRQ                   62
