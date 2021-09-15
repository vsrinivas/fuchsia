// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_AML_PWM_REGULATOR_AML_PWM_REGULATOR_H_
#define SRC_DEVICES_POWER_DRIVERS_AML_PWM_REGULATOR_AML_PWM_REGULATOR_H_

#include <fidl/fuchsia.hardware.vreg/cpp/wire.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <fuchsia/hardware/vreg/cpp/banjo.h>
#include <lib/ddk/debug.h>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <soc/aml-common/aml-pwm-regs.h>

namespace aml_pwm_regulator {

class AmlPwmRegulator;
using AmlPwmRegulatorType = ddk::Device<AmlPwmRegulator>;
using fuchsia_hardware_vreg::wire::PwmVregMetadataEntry;

class AmlPwmRegulator : public AmlPwmRegulatorType,
                        public ddk::VregProtocol<AmlPwmRegulator, ddk::base_protocol> {
 public:
  explicit AmlPwmRegulator(zx_device_t* parent, const PwmVregMetadataEntry& vreg_range,
                           ddk::PwmProtocolClient pwm)
      : AmlPwmRegulatorType(parent),
        pwm_index_(vreg_range.pwm_index()),
        period_ns_(vreg_range.period_ns()),
        min_voltage_uv_(vreg_range.min_voltage_uv()),
        voltage_step_uv_(vreg_range.voltage_step_uv()),
        num_steps_(vreg_range.num_steps()),
        current_step_(vreg_range.num_steps()),
        pwm_(pwm) {}
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device Protocol Implementation
  void DdkRelease() { delete this; }

  // Vreg Implementation.
  zx_status_t VregSetVoltageStep(uint32_t step);
  uint32_t VregGetVoltageStep();
  void VregGetRegulatorParams(vreg_params_t* out_params);

 private:
  friend class FakePwmRegulator;

  uint32_t pwm_index_;
  uint32_t period_ns_;
  uint32_t min_voltage_uv_;
  uint32_t voltage_step_uv_;
  uint32_t num_steps_;

  uint32_t current_step_;

  ddk::PwmProtocolClient pwm_;
};

}  // namespace aml_pwm_regulator

#endif  // SRC_DEVICES_POWER_DRIVERS_AML_PWM_REGULATOR_AML_PWM_REGULATOR_H_
