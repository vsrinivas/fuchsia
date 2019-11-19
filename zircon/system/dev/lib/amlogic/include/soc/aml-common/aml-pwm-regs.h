// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_PWM_REGS_H_
#define ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_PWM_REGS_H_

namespace aml_pwm {

constexpr uint32_t kAOffset = 0x0;
constexpr uint32_t kBOffset = 0x4;
constexpr uint32_t kMiscOffset = 0x8;
constexpr uint32_t kDSOffset = 0xc;
constexpr uint32_t kTimeOffset = 0x10;
constexpr uint32_t kA2Offset = 0x14;
constexpr uint32_t kB2Offset = 0x18;
constexpr uint32_t kBlinkOffset = 0x1c;

// Mode indices
enum Mode : uint32_t {
  OFF = 0,
  ON = 1,
  DELTA_SIGMA = 2,
  TWO_TIMER = 3,
  UNKNOWN = 4,
};

struct mode_config {
  uint32_t mode;
  mode_config& operator=(const mode_config& other) = default;
  bool operator==(const mode_config& other) const { return (mode == other.mode); }
};

struct mode_config_delta_sigma {
  uint32_t mode;
  uint16_t delta;
  mode_config_delta_sigma& operator=(const mode_config_delta_sigma& other) = default;
  bool operator==(const mode_config_delta_sigma& other) const {
    return (mode == other.mode) && (delta == other.delta);
  }
};

struct mode_config_two_timer {
  uint32_t mode;
  float duty_cycle2;
  uint8_t timer1;
  uint8_t timer2;
  mode_config_two_timer& operator=(const mode_config_two_timer& other) = default;
  bool operator==(const mode_config_two_timer& other) const {
    return (mode == other.mode) && (duty_cycle2 == other.duty_cycle2) && (timer1 == other.timer1) &&
           (timer2 == other.timer2);
  }
};

}  // namespace aml_pwm

#endif  // ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_PWM_REGS_H_
