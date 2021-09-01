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
  explicit AmlPwmRegulator(zx_device_t* parent, PwmVregMetadataEntry vreg_range,
                           ddk::PwmProtocolClient pwm)
      : AmlPwmRegulatorType(parent),
        vreg_range_(vreg_range),
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

  PwmVregMetadataEntry vreg_range_;
  uint32_t current_step_;

  ddk::PwmProtocolClient pwm_;
};

}  // namespace aml_pwm_regulator

#endif  // SRC_DEVICES_POWER_DRIVERS_AML_PWM_REGULATOR_AML_PWM_REGULATOR_H_
