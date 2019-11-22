// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#define MT8183_MCUCFG_BASE  0x0c530000
#define MT8183_MCUCFG_SIZE  0x2000

#define MT8183_GPIO_BASE 0x10005000
#define MT8183_GPIO_SIZE 0x1000

#define MT8183_EINT_BASE 0x1000b000
#define MT8183_EINT_SIZE 0x1000

#define MT8183_MSDC0_BASE 0x11230000
#define MT8183_MSDC0_SIZE 0x1000

// MCU config interrupt polarity registers start
#define MT8183_MCUCFG_INT_POL_CTL0 0xa80

// GIC interrupt numbers
#define MT8183_IRQ_MSDC0 109
#define MT8183_IRQ_EINT 209

// GPIOs
#define MT8183_GPIO_MSDC0_RST 133
