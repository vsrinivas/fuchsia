// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_PWM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_PWM_H_

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-common/aml-pwm-regs.h>

#define A311D_PWM_A 0
#define A311D_PWM_B 1
#define A311D_PWM_C 2
#define A311D_PWM_D 3
#define A311D_PWM_E 4
#define A311D_PWM_F 5
#define A311D_PWM_AO_A 6
#define A311D_PWM_AO_B 7
#define A311D_PWM_AO_C 8
#define A311D_PWM_AO_D 9
#define A311D_PWM_COUNT 10

namespace aml_pwm {

// Offsets
static_assert((A311D_PWM_PWM_A == kAOffset) && (A311D_PWM_PWM_C == kAOffset) &&
                  (A311D_PWM_PWM_E == kAOffset) && (A311D_AO_PWM_PWM_A == kAOffset) &&
                  (A311D_AO_PWM_PWM_C == kAOffset),
              "PWM_PWM_A offset incorrect!\n");
static_assert((A311D_PWM_PWM_B == kBOffset) && (A311D_PWM_PWM_D == kBOffset) &&
                  (A311D_PWM_PWM_F == kBOffset) && (A311D_AO_PWM_PWM_B == kBOffset) &&
                  (A311D_AO_PWM_PWM_D == kBOffset),
              "PWM_PWM_B offset incorrect!\n");
static_assert((A311D_PWM_MISC_REG_AB == kMiscOffset) && (A311D_PWM_MISC_REG_CD == kMiscOffset) &&
                  (A311D_PWM_MISC_REG_EF == kMiscOffset) &&
                  (A311D_AO_PWM_MISC_REG_AB == kMiscOffset) &&
                  (A311D_AO_PWM_MISC_REG_CD == kMiscOffset),
              "MISC offset incorrect!\n");
static_assert((A311D_DS_A_B == kDSOffset) && (A311D_DS_C_D == kDSOffset) &&
                  (A311D_DS_E_F == kDSOffset) && (A311D_AO_DS_A_B == kDSOffset) &&
                  (A311D_AO_DS_C_D == kDSOffset),
              "DS offset incorrect!\n");
static_assert((A311D_PWM_TIME_AB == kTimeOffset) && (A311D_PWM_TIME_CD == kTimeOffset) &&
                  (A311D_PWM_TIME_EF == kTimeOffset) && (A311D_AO_PWM_TIME_AB == kTimeOffset) &&
                  (A311D_AO_PWM_TIME_CD == kTimeOffset),
              "Time offset incorrect!\n");
static_assert((A311D_PWM_A2 == kA2Offset) && (A311D_PWM_C2 == kA2Offset) &&
                  (A311D_PWM_E2 == kA2Offset) && (A311D_AO_PWM_A2 == kA2Offset) &&
                  (A311D_AO_PWM_C2 == kA2Offset),
              "A2 offset incorrect!\n");
static_assert((A311D_PWM_B2 == kB2Offset) && (A311D_PWM_D2 == kB2Offset) &&
                  (A311D_PWM_F2 == kB2Offset) && (A311D_AO_PWM_B2 == kB2Offset) &&
                  (A311D_AO_PWM_D2 == kB2Offset),
              "B2 offset incorrect!\n");
static_assert((A311D_PWM_BLINK_AB == kBlinkOffset) && (A311D_PWM_BLINK_CD == kBlinkOffset) &&
                  (A311D_PWM_BLINK_EF == kBlinkOffset) && (A311D_AO_PWM_BLINK_AB == kBlinkOffset) &&
                  (A311D_AO_PWM_BLINK_CD == kBlinkOffset),
              "Blink offset incorrect!\n");

}  // namespace aml_pwm

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_PWM_H_
