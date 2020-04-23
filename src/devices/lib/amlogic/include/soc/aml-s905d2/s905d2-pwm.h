// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_PWM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_PWM_H_

#include <soc/aml-common/aml-pwm-regs.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#define S905D2_PWM_A 0
#define S905D2_PWM_B 1
#define S905D2_PWM_C 2
#define S905D2_PWM_D 3
#define S905D2_PWM_E 4
#define S905D2_PWM_F 5
#define S905D2_PWM_AO_A 6
#define S905D2_PWM_AO_B 7
#define S905D2_PWM_AO_C 8
#define S905D2_PWM_AO_D 9
#define S905D2_PWM_COUNT 10

namespace aml_pwm {

// Offsets
static_assert(((S905D2_PWM_PWM_A - 0x6c00) * 4 == kAOffset) &&
                  ((S905D2_PWM_PWM_C - 0x6800) * 4 == kAOffset) &&
                  ((S905D2_PWM_PWM_E - 0x6400) * 4 == kAOffset) &&
                  (S905D2_AO_PWM_PWM_A == kAOffset) && (S905D2_AO_PWM_PWM_C == kAOffset),
              "PWM_PWM_A offset incorrect!\n");
static_assert(((S905D2_PWM_PWM_B - 0x6c00) * 4 == kBOffset) &&
                  ((S905D2_PWM_PWM_D - 0x6800) * 4 == kBOffset) &&
                  ((S905D2_PWM_PWM_F - 0x6400) * 4 == kBOffset) &&
                  (S905D2_AO_PWM_PWM_B == kBOffset) && (S905D2_AO_PWM_PWM_D == kBOffset),
              "PWM_PWM_B offset incorrect!\n");
static_assert(((S905D2_PWM_MISC_REG_AB - 0x6c00) * 4 == kMiscOffset) &&
                  ((S905D2_PWM_MISC_REG_CD - 0x6800) * 4 == kMiscOffset) &&
                  ((S905D2_PWM_MISC_REG_EF - 0x6400) * 4 == kMiscOffset) &&
                  (S905D2_AO_PWM_MISC_REG_AB == kMiscOffset) &&
                  (S905D2_AO_PWM_MISC_REG_CD == kMiscOffset),
              "MISC offset incorrect!\n");
static_assert(((S905D2_DS_A_B - 0x6c00) * 4 == kDSOffset) &&
                  ((S905D2_DS_C_D - 0x6800) * 4 == kDSOffset) &&
                  ((S905D2_DS_E_F - 0x6400) * 4 == kDSOffset) && (S905D2_AO_DS_A_B == kDSOffset) &&
                  (S905D2_AO_DS_C_D == kDSOffset),
              "DS offset incorrect!\n");
static_assert(((S905D2_PWM_TIME_AB - 0x6c00) * 4 == kTimeOffset) &&
                  ((S905D2_PWM_TIME_CD - 0x6800) * 4 == kTimeOffset) &&
                  ((S905D2_PWM_TIME_EF - 0x6400) * 4 == kTimeOffset) &&
                  (S905D2_AO_PWM_TIME_AB == kTimeOffset) && (S905D2_AO_PWM_TIME_CD == kTimeOffset),
              "Time offset incorrect!\n");
static_assert(((S905D2_PWM_A2 - 0x6c00) * 4 == kA2Offset) &&
                  ((S905D2_PWM_C2 - 0x6800) * 4 == kA2Offset) &&
                  ((S905D2_PWM_E2 - 0x6400) * 4 == kA2Offset) && (S905D2_AO_PWM_A2 == kA2Offset) &&
                  (S905D2_AO_PWM_C2 == kA2Offset),
              "A2 offset incorrect!\n");
static_assert(((S905D2_PWM_B2 - 0x6c00) * 4 == kB2Offset) &&
                  ((S905D2_PWM_D2 - 0x6800) * 4 == kB2Offset) &&
                  ((S905D2_PWM_F2 - 0x6400) * 4 == kB2Offset) && (S905D2_AO_PWM_B2 == kB2Offset) &&
                  (S905D2_AO_PWM_D2 == kB2Offset),
              "B2 offset incorrect!\n");
static_assert(((S905D2_PWM_BLINK_AB - 0x6c00) * 4 == kBlinkOffset) &&
                  ((S905D2_PWM_BLINK_CD - 0x6800) * 4 == kBlinkOffset) &&
                  ((S905D2_PWM_BLINK_EF - 0x6400) * 4 == kBlinkOffset) &&
                  (S905D2_AO_PWM_BLINK_AB == kBlinkOffset) &&
                  (S905D2_AO_PWM_BLINK_CD == kBlinkOffset),
              "Blink offset incorrect!\n");

}  // namespace aml_pwm

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_PWM_H_
