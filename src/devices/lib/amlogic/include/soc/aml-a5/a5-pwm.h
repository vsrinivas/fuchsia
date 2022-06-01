// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_PWM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_PWM_H_

#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-common/aml-pwm-regs.h>

#define A5_PWM_A 0
#define A5_PWM_B 1
#define A5_PWM_C 2
#define A5_PWM_D 3
#define A5_PWM_E 4
#define A5_PWM_F 5
#define A5_PWM_G 6
#define A5_PWM_H 7
#define A5_PWM_COUNT 8

namespace aml_pwm {

// Offsets
static_assert((A5_PWM_PWM_A == kAOffset) && (A5_PWM_PWM_C == kAOffset) &&
                  (A5_PWM_PWM_E == kAOffset) && (A5_PWM_PWM_G == kAOffset),
              "PWM_PWM_A offset incorrect!\n");
static_assert((A5_PWM_PWM_B == kBOffset) && (A5_PWM_PWM_D == kBOffset) &&
                  (A5_PWM_PWM_F == kBOffset) && (A5_PWM_PWM_H == kBOffset),
              "PWM_PWM_B offset incorrect!\n");
static_assert((A5_PWM_MISC_REG_AB == kMiscOffset) && (A5_PWM_MISC_REG_CD == kMiscOffset) &&
                  (A5_PWM_MISC_REG_EF == kMiscOffset) && (A5_PWM_MISC_REG_GH == kMiscOffset),
              "MISC offset incorrect!\n");
static_assert((A5_DS_A_B == kDSOffset) && (A5_DS_C_D == kDSOffset) && (A5_DS_E_F == kDSOffset) &&
                  (A5_DS_G_H == kDSOffset),
              "DS offset incorrect!\n");
static_assert((A5_PWM_TIME_AB == kTimeOffset) && (A5_PWM_TIME_CD == kTimeOffset) &&
                  (A5_PWM_TIME_EF == kTimeOffset) && (A5_PWM_TIME_GH == kTimeOffset),
              "Time offset incorrect!\n");
static_assert((A5_PWM_A2 == kA2Offset) && (A5_PWM_C2 == kA2Offset) && (A5_PWM_E2 == kA2Offset) &&
                  (A5_PWM_G2 == kA2Offset),
              "A2 offset incorrect!\n");
static_assert((A5_PWM_B2 == kB2Offset) && (A5_PWM_D2 == kB2Offset) && (A5_PWM_F2 == kB2Offset) &&
                  (A5_PWM_H2 == kB2Offset),
              "B2 offset incorrect!\n");
static_assert((A5_PWM_BLINK_AB == kBlinkOffset) && (A5_PWM_BLINK_CD == kBlinkOffset) &&
                  (A5_PWM_BLINK_EF == kBlinkOffset) && (A5_PWM_BLINK_GH == kBlinkOffset),
              "Blink offset incorrect!\n");
static_assert((A5_PWM_LOCK_AB == kLockOffset) && (A5_PWM_LOCK_CD == kLockOffset) &&
                  (A5_PWM_LOCK_EF == kLockOffset) && (A5_PWM_LOCK_GH == kLockOffset),
              "Lock offset incorrect!\n");

}  // namespace aml_pwm

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_PWM_H_
