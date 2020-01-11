// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_PWM_H_
#define ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_PWM_H_

#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <optional>

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pwm.h>
#include <fbl/mutex.h>
#include <hwreg/mmio.h>

namespace thermal {

namespace {

// MMIO index.
constexpr uint32_t kPwmAOCDMmio = 3;
constexpr uint32_t kPwmABMmio = 4;

// Input clock frequency
constexpr uint32_t kXtalFreq = 24000000;

}  // namespace

// This class represents a generic PWM
// which provides interface to set the
// period and configure to appropriate
// duty cycle.
class AmlPwm {
 public:
  enum PwmType {
    PWM_AO_CD,
    PWM_AB,
  };

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlPwm);
  AmlPwm() = default;
  // For testing
  void MapMmio(ddk::MmioBuffer pwm_mmio) { pwm_mmio_ = std::move(pwm_mmio); }
  // pwm_type selects between PWM AB/CD, 0 chooses AB
  //                                     1 chooses CD
  // Note: This Create is slightly different from the others in that it doesn't call
  //       Init because PWM is initialized from Voltage Regulator rather than the
  //       thermal device.
  zx_status_t Create(zx_device_t* parent, PwmType pwm_type);
  // period in units of nanoseconds
  // hwpwm selects between PWM (A & B)/(C & D), 0 chooses A/C
  //                                            1 chooses B/D
  zx_status_t Init(uint32_t period_ns, uint32_t hwpwm);
  zx_status_t Configure(uint32_t duty_cycle);
  zx_status_t SetConfig(const pwm_config_t* config) {
    return Configure(static_cast<uint32_t>(config->duty_cycle));
  }

 private:
  uint32_t period_ns_;
  uint32_t duty_cycle_ = 101;
  uint32_t pwm_duty_cycle_offset_;
  uint32_t enable_bit_;
  uint32_t clk_enable_bit_;
  uint32_t constant_enable_bit_;
  std::optional<ddk::MmioBuffer> pwm_mmio_;
  fbl::Mutex pwm_lock_;
};
}  // namespace thermal

#endif  // ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_PWM_H_
