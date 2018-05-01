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

#define S905D2_GPIO_IRQ_0    96
#define S905D2_GPIO_IRQ_1    97
#define S905D2_GPIO_IRQ_2    98
#define S905D2_GPIO_IRQ_3    99
#define S905D2_GPIO_IRQ_4    100
#define S905D2_GPIO_IRQ_5    101
#define S905D2_GPIO_IRQ_6    102
#define S905D2_GPIO_IRQ_7    103

#define S905D2_A0_GPIO_IRQ_0 234
#define S905D2_A0_GPIO_IRQ_1 239
