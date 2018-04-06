// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define IMX_GPIO_BLOCKS             5
#define IMX_NUM_TO_BLOCK(x)         (x / 32)
#define IMX_NUM_TO_BIT(x)           (x % 32)

// IMX8M blocks are 1 based
#define IMX_GPIO_PIN(block, num)  ((block-1) * 32 + num)

#define GPIO_INPUT                      (0)
#define GPIO_OUTPUT                     (1)

#define IMX_GPIO_DR                     (0x0000)
#define IMX_GPIO_GDIR                   (0x0004)
#define IMX_GPIO_PSR                    (0x0008)
#define IMX_GPIO_ICR1                   (0x000C)
#define IMX_GPIO_ICR2                   (0x0010)
#define IMX_GPIO_IMR                    (0x0014)
#define IMX_GPIO_ISR                    (0x0018)
#define IMX_GPIO_EDGE_SEL               (0x001C)
