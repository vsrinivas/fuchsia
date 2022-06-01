// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_PWM_REGS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_PWM_REGS_H_

namespace aml_pwm {

enum RegIdx : uint8_t {
  REG_A = 0,
  REG_B = 1,
  REG_MISC = 2,
  REG_DS = 3,
  REG_TIME = 4,
  REG_A2 = 5,
  REG_B2 = 6,
  REG_BLINK = 7,
  REG_LOCK = 8,

  REG_COUNT,
};

constexpr uint32_t kAOffset = 0x0;
constexpr uint32_t kBOffset = 0x4;
constexpr uint32_t kMiscOffset = 0x8;
constexpr uint32_t kDSOffset = 0xc;
constexpr uint32_t kTimeOffset = 0x10;
constexpr uint32_t kA2Offset = 0x14;
constexpr uint32_t kB2Offset = 0x18;
constexpr uint32_t kBlinkOffset = 0x1c;
constexpr uint32_t kLockOffset = 0x20;

// Mode indices
enum Mode : uint32_t {
  OFF = 0,
  ON = 1,
  DELTA_SIGMA = 2,
  TWO_TIMER = 3,
  UNKNOWN = 4,
};

struct mode_config_regular {};

struct mode_config_delta_sigma {
  uint16_t delta;
};

struct mode_config_two_timer {
  uint32_t period_ns2;
  float duty_cycle2;
  uint8_t timer1;
  uint8_t timer2;
};

struct mode_config {
  uint32_t mode;
  union {
    struct mode_config_regular regular;
    struct mode_config_delta_sigma delta_sigma;
    struct mode_config_two_timer two_timer;
  };
};

}  // namespace aml_pwm

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_PWM_REGS_H_
