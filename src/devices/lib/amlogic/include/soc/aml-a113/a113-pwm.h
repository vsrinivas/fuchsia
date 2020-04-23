// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A113_A113_PWM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A113_A113_PWM_H_

#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-common/aml-pwm-regs.h>

#define A113_PWM_A 0
#define A113_PWM_B 1
#define A113_PWM_C 2
#define A113_PWM_D 3
#define A113_PWM_AO_A 4
#define A113_PWM_AO_B 5
#define A113_PWM_AO_C 6
#define A113_PWM_AO_D 7
#define A113_PWM_COUNT 8

namespace aml_pwm {

// Offsets
static_assert((A113_PWM_PWM_A == kAOffset) && (A113_PWM_PWM_C == kAOffset) &&
                  (A113_AO_PWM_PWM_A == kAOffset) && (A113_AO_PWM_PWM_C == kAOffset),
              "PWM_PWM_A offset incorrect!\n");
static_assert((A113_PWM_PWM_B == kBOffset) && (A113_PWM_PWM_D == kBOffset) &&
                  (A113_AO_PWM_PWM_B == kBOffset) && (A113_AO_PWM_PWM_D == kBOffset),
              "PWM_PWM_B offset incorrect!\n");
static_assert((A113_PWM_MISC_REG_AB == kMiscOffset) && (A113_PWM_MISC_REG_CD == kMiscOffset) &&
                  (A113_AO_PWM_MISC_REG_AB == kMiscOffset) &&
                  (A113_AO_PWM_MISC_REG_CD == kMiscOffset),
              "MISC offset incorrect!\n");
static_assert((A113_DS_A_B == kDSOffset) && (A113_DS_C_D == kDSOffset) &&
                  (A113_AO_DS_A_B == kDSOffset) && (A113_AO_DS_C_D == kDSOffset),
              "DS offset incorrect!\n");
static_assert((A113_PWM_TIME_AB == kTimeOffset) && (A113_PWM_TIME_CD == kTimeOffset) &&
                  (A113_AO_PWM_TIME_AB == kTimeOffset) && (A113_AO_PWM_TIME_CD == kTimeOffset),
              "Time offset incorrect!\n");
static_assert((A113_PWM_A2 == kA2Offset) && (A113_PWM_C2 == kA2Offset) &&
                  (A113_AO_PWM_A2 == kA2Offset) && (A113_AO_PWM_C2 == kA2Offset),
              "A2 offset incorrect!\n");
static_assert((A113_PWM_B2 == kB2Offset) && (A113_PWM_D2 == kB2Offset) &&
                  (A113_AO_PWM_B2 == kB2Offset) && (A113_AO_PWM_D2 == kB2Offset),
              "B2 offset incorrect!\n");
static_assert((A113_PWM_BLINK_AB == kBlinkOffset) && (A113_PWM_BLINK_CD == kBlinkOffset) &&
                  (A113_AO_PWM_BLINK_AB == kBlinkOffset) && (A113_AO_PWM_BLINK_CD == kBlinkOffset),
              "Blink offset incorrect!\n");

}  // namespace aml_pwm

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A113_A113_PWM_H_
