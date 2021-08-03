// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/aml-pwm-regulator/aml-pwm-regulator.h"

#include <fuchsia/hardware/pwm/cpp/banjo-mock.h>

#include "src/devices/lib/metadata/llcpp/vreg.h"

bool operator==(const pwm_config_t& lhs, const pwm_config_t& rhs) {
  return (lhs.polarity == rhs.polarity) && (lhs.period_ns == rhs.period_ns) &&
         (lhs.duty_cycle == rhs.duty_cycle) && (lhs.mode_config_size == rhs.mode_config_size) &&
         (reinterpret_cast<aml_pwm::mode_config*>(lhs.mode_config_buffer)->mode ==
          reinterpret_cast<aml_pwm::mode_config*>(rhs.mode_config_buffer)->mode);
}

namespace aml_pwm_regulator {

class TestPwmRegulator : public AmlPwmRegulator {
 public:
  static std::unique_ptr<TestPwmRegulator> Create(PwmVregMetadataEntry vreg_range,
                                                  const pwm_protocol_t* pwm) {
    return std::make_unique<TestPwmRegulator>(vreg_range, ddk::PwmProtocolClient(pwm));
  }

  explicit TestPwmRegulator(PwmVregMetadataEntry vreg_range, ddk::PwmProtocolClient pwm)
      : AmlPwmRegulator(nullptr, vreg_range, pwm) {}
};

TEST(AmlPwmRegulatorTest, RegulatorTest) {
  fidl::Arena<2048> allocator;
  ddk::MockPwm pwm_;
  auto regulator_ = TestPwmRegulator::Create(
      vreg::BuildMetadata(allocator, 0, 1250, 690'000, 1'000, 11), pwm_.GetProto());
  ASSERT_NOT_NULL(regulator_);

  vreg_params_t params;
  regulator_->VregGetRegulatorParams(&params);
  EXPECT_EQ(params.min_uv, 690'000);
  EXPECT_EQ(params.num_steps, 11);
  EXPECT_EQ(params.step_size_uv, 1'000);

  EXPECT_EQ(regulator_->VregGetVoltageStep(), 11);

  aml_pwm::mode_config mode = {
      .mode = aml_pwm::ON,
      .regular = {},
  };
  pwm_config_t cfg = {
      .polarity = false,
      .period_ns = 1250,
      .duty_cycle = 70,
      .mode_config_buffer = reinterpret_cast<uint8_t*>(&mode),
      .mode_config_size = sizeof(mode),
  };
  pwm_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(regulator_->VregSetVoltageStep(3));
  EXPECT_EQ(regulator_->VregGetVoltageStep(), 3);

  cfg.duty_cycle = 10;
  pwm_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(regulator_->VregSetVoltageStep(9));
  EXPECT_EQ(regulator_->VregGetVoltageStep(), 9);

  EXPECT_NOT_OK(regulator_->VregSetVoltageStep(14));

  pwm_.VerifyAndClear();
}

}  // namespace aml_pwm_regulator
