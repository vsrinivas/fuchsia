// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-light.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/alloc_checker.h>
#include <mock/ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/pwm.h>

bool operator==(const pwm_config_t& lhs, const pwm_config_t& rhs) {
  return (lhs.polarity == rhs.polarity) && (lhs.period_ns == rhs.period_ns) &&
         (lhs.duty_cycle == rhs.duty_cycle) && (lhs.mode_config_size == rhs.mode_config_size) &&
         (static_cast<aml_pwm::mode_config*>(lhs.mode_config_buffer)->mode ==
          static_cast<aml_pwm::mode_config*>(rhs.mode_config_buffer)->mode);
}

namespace aml_light {

class FakeAmlLight : public AmlLight {
 public:
  static std::unique_ptr<FakeAmlLight> Create(const gpio_protocol_t* gpio,
                                              std::optional<const pwm_protocol_t*> pwm) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeAmlLight>(&ac);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed\n", __func__);
      return nullptr;
    }
    device->lights_.emplace_back(
        "test", ddk::GpioProtocolClient(gpio),
        pwm.has_value() ? std::optional<ddk::PwmProtocolClient>(*pwm) : std::nullopt);
    device->lights_.back().Init(true);
    return device;
  }

  explicit FakeAmlLight() : AmlLight(nullptr) {}
};

class AmlLightTest : public zxtest::Test {
 public:
  void TearDown() override {
    gpio_.VerifyAndClear();
    pwm_.VerifyAndClear();
  }

 protected:
  std::unique_ptr<FakeAmlLight> light_;

  ddk::MockGpio gpio_;
  ddk::MockPwm pwm_;
};

TEST_F(AmlLightTest, NonBrightnessTest) {
  gpio_.ExpectConfigOut(ZX_OK, 1);

  auto gpio = gpio_.GetProto();
  light_ = FakeAmlLight::Create(gpio, std::nullopt);
  ASSERT_NOT_NULL(light_);
}

TEST_F(AmlLightTest, BrightnessTest) {
  pwm_.ExpectEnable(ZX_OK);
  aml_pwm::mode_config regular = {aml_pwm::ON, {}};
  pwm_config_t init_config = {false, 1250, 100.0, &regular, sizeof(regular)};
  pwm_.ExpectSetConfig(ZX_OK, init_config);

  auto gpio = gpio_.GetProto();
  auto pwm = pwm_.GetProto();
  light_ = FakeAmlLight::Create(gpio, pwm);
  ASSERT_NOT_NULL(light_);
}

}  // namespace aml_light
