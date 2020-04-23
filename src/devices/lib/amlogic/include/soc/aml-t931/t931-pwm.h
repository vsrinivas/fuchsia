// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_T931_T931_PWM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_T931_T931_PWM_H_

#include <soc/aml-common/aml-pwm-regs.h>
#include <soc/aml-t931/t931-hw.h>

#define T931_PWM_A 0
#define T931_PWM_B 1
#define T931_PWM_C 2
#define T931_PWM_D 3
#define T931_PWM_E 4
#define T931_PWM_F 5
#define T931_PWM_AO_A 6
#define T931_PWM_AO_B 7
#define T931_PWM_AO_C 8
#define T931_PWM_AO_D 9
#define T931_PWM_COUNT 10

namespace aml_pwm {

// Offsets
static_assert((T931_PWM_PWM_A == kAOffset) && (T931_PWM_PWM_C == kAOffset) &&
                  (T931_PWM_PWM_E == kAOffset) && (T931_AO_PWM_PWM_A == kAOffset) &&
                  (T931_AO_PWM_PWM_C == kAOffset),
              "PWM_PWM_A offset incorrect!\n");
static_assert((T931_PWM_PWM_B == kBOffset) && (T931_PWM_PWM_D == kBOffset) &&
                  (T931_PWM_PWM_F == kBOffset) && (T931_AO_PWM_PWM_B == kBOffset) &&
                  (T931_AO_PWM_PWM_D == kBOffset),
              "PWM_PWM_B offset incorrect!\n");
static_assert((T931_PWM_MISC_REG_AB == kMiscOffset) && (T931_PWM_MISC_REG_CD == kMiscOffset) &&
                  (T931_PWM_MISC_REG_EF == kMiscOffset) &&
                  (T931_AO_PWM_MISC_REG_AB == kMiscOffset) &&
                  (T931_AO_PWM_MISC_REG_CD == kMiscOffset),
              "MISC offset incorrect!\n");
static_assert((T931_DS_A_B == kDSOffset) && (T931_DS_C_D == kDSOffset) &&
                  (T931_DS_E_F == kDSOffset) && (T931_AO_DS_A_B == kDSOffset) &&
                  (T931_AO_DS_C_D == kDSOffset),
              "DS offset incorrect!\n");
static_assert((T931_PWM_TIME_AB == kTimeOffset) && (T931_PWM_TIME_CD == kTimeOffset) &&
                  (T931_PWM_TIME_EF == kTimeOffset) && (T931_AO_PWM_TIME_AB == kTimeOffset) &&
                  (T931_AO_PWM_TIME_CD == kTimeOffset),
              "Time offset incorrect!\n");
static_assert((T931_PWM_A2 == kA2Offset) && (T931_PWM_C2 == kA2Offset) &&
                  (T931_PWM_E2 == kA2Offset) && (T931_AO_PWM_A2 == kA2Offset) &&
                  (T931_AO_PWM_C2 == kA2Offset),
              "A2 offset incorrect!\n");
static_assert((T931_PWM_B2 == kB2Offset) && (T931_PWM_D2 == kB2Offset) &&
                  (T931_PWM_F2 == kB2Offset) && (T931_AO_PWM_B2 == kB2Offset) &&
                  (T931_AO_PWM_D2 == kB2Offset),
              "B2 offset incorrect!\n");
static_assert((T931_PWM_BLINK_AB == kBlinkOffset) && (T931_PWM_BLINK_CD == kBlinkOffset) &&
                  (T931_PWM_BLINK_EF == kBlinkOffset) && (T931_AO_PWM_BLINK_AB == kBlinkOffset) &&
                  (T931_AO_PWM_BLINK_CD == kBlinkOffset),
              "Blink offset incorrect!\n");

}  // namespace aml_pwm

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_T931_T931_PWM_H_
