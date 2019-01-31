// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define BIT(x)                              (1UL << (x))
#define NSEC_PER_SEC                        1000000000

// Register offset
#define S905D2_AO_PWM_PWM_A                 (0x0 * 4)
#define S905D2_AO_PWM_PWM_B                 (0x1 * 4)
#define S905D2_AO_PWM_MISC_REG_AB           (0x2 * 4)
#define S905D2_AO_DS_A_B                    (0x3 * 4)
#define S905D2_AO_PWM_TIME_AB               (0x4 * 4)
#define S905D2_AO_PWM_A2                    (0x5 * 4)
#define S905D2_AO_PWM_B2                    (0x6 * 4)
#define S905D2_AO_PWM_BLINK_AB              (0x7 * 4)

#define CLK_A_ENABLE                        BIT(15)
#define CLK_B_ENABLE                        BIT(23)
#define A_ENABLE                            BIT(0)
#define B_ENABLE                            BIT(1)

#define PWM_HIGH_SHIFT                      16
