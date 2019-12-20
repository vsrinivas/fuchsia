// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-light.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-async/cpp/bind.h>

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

  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::Bind(dispatcher, std::move(request), this);
  }
};

class AmlLightTest : public zxtest::Test {
 public:
  void Init() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    ASSERT_OK(loop_->StartThread("aml-light-test-loop"));
    ASSERT_OK(light_->Connect(loop_->dispatcher(), std::move(server)));
  }

  void TearDown() override {
    gpio_.VerifyAndClear();
    pwm_.VerifyAndClear();

    loop_->Quit();
    loop_->JoinThreads();
  }

 protected:
  friend class FakeAmlLight;

  std::unique_ptr<FakeAmlLight> light_;

  ddk::MockGpio gpio_;
  ddk::MockPwm pwm_;

  zx::channel client_;

 private:
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(AmlLightTest, GetNameTest) {
  pwm_.ExpectEnable(ZX_OK);
  aml_pwm::mode_config regular = {aml_pwm::ON, {}};
  pwm_config_t init_config = {false, 1250, 100.0, &regular, sizeof(regular)};
  pwm_.ExpectSetConfig(ZX_OK, init_config);

  auto gpio = gpio_.GetProto();
  auto pwm = pwm_.GetProto();
  light_ = FakeAmlLight::Create(gpio, pwm);
  ASSERT_NOT_NULL(light_);
  Init();

  ::llcpp::fuchsia::hardware::light::Light::SyncClient client(std::move(client_));
  auto result = client.GetName(0);
  EXPECT_OK(result.status());
  EXPECT_EQ(strcmp(result.Unwrap()->name.begin(), "test"), 0);
}

TEST_F(AmlLightTest, HasCapabilityTest1) {
  gpio_.ExpectWrite(ZX_OK, 255);

  auto gpio = gpio_.GetProto();
  light_ = FakeAmlLight::Create(gpio, std::nullopt);
  ASSERT_NOT_NULL(light_);
  Init();

  ::llcpp::fuchsia::hardware::light::Light::SyncClient client(std::move(client_));
  auto result = client.HasCapability(0, Capability::BRIGHTNESS);
  EXPECT_OK(result.status());
  EXPECT_FALSE(result.Unwrap()->has);

  result = client.HasCapability(0, Capability::RGB);
  EXPECT_OK(result.status());
  EXPECT_FALSE(result.Unwrap()->has);
}

TEST_F(AmlLightTest, HasCapabilityTest2) {
  pwm_.ExpectEnable(ZX_OK);
  aml_pwm::mode_config regular = {aml_pwm::ON, {}};
  pwm_config_t init_config = {false, 1250, 100.0, &regular, sizeof(regular)};
  pwm_.ExpectSetConfig(ZX_OK, init_config);

  auto gpio = gpio_.GetProto();
  auto pwm = pwm_.GetProto();
  light_ = FakeAmlLight::Create(gpio, pwm);
  ASSERT_NOT_NULL(light_);
  Init();

  ::llcpp::fuchsia::hardware::light::Light::SyncClient client(std::move(client_));
  auto result = client.HasCapability(0, Capability::BRIGHTNESS);
  EXPECT_OK(result.status());
  EXPECT_TRUE(result.Unwrap()->has);

  result = client.HasCapability(0, Capability::RGB);
  EXPECT_OK(result.status());
  EXPECT_FALSE(result.Unwrap()->has);
}

TEST_F(AmlLightTest, SetValueTest1) {
  gpio_.ExpectWrite(ZX_OK, 255);

  auto gpio = gpio_.GetProto();
  light_ = FakeAmlLight::Create(gpio, std::nullopt);
  ASSERT_NOT_NULL(light_);
  Init();

  ::llcpp::fuchsia::hardware::light::Light::SyncClient client(std::move(client_));
  auto get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 255);

  get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 255);

  gpio_.ExpectWrite(ZX_OK, 0);
  auto set_result = client.SetSimpleValue(0, 0);
  EXPECT_OK(set_result.status());

  get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 0);

  gpio_.ExpectWrite(ZX_OK, 1);
  set_result = client.SetSimpleValue(0, 1);
  EXPECT_OK(set_result.status());

  gpio_.ExpectWrite(ZX_OK, 1);
  set_result = client.SetSimpleValue(0, 1);
  EXPECT_OK(set_result.status());

  get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 1);
}

TEST_F(AmlLightTest, SetValueTest2) {
  pwm_.ExpectEnable(ZX_OK);
  aml_pwm::mode_config regular = {aml_pwm::ON, {}};
  pwm_config_t config = {false, 1250, 100.0, &regular, sizeof(regular)};
  pwm_.ExpectSetConfig(ZX_OK, config);

  auto gpio = gpio_.GetProto();
  auto pwm = pwm_.GetProto();
  light_ = FakeAmlLight::Create(gpio, pwm);
  ASSERT_NOT_NULL(light_);
  Init();

  ::llcpp::fuchsia::hardware::light::Light::SyncClient client(std::move(client_));
  auto get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 255);

  get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 255);

  config.duty_cycle = 0;
  pwm_.ExpectSetConfig(ZX_OK, config);
  auto set_result = client.SetSimpleValue(0, 0);
  EXPECT_OK(set_result.status());

  get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 0);

  config.duty_cycle = 20.0;
  pwm_.ExpectSetConfig(ZX_OK, config);
  set_result = client.SetSimpleValue(0, 51);
  EXPECT_OK(set_result.status());

  pwm_.ExpectSetConfig(ZX_OK, config);
  set_result = client.SetSimpleValue(0, 51);
  EXPECT_OK(set_result.status());

  get_result = client.GetSimpleValue(0);
  EXPECT_OK(get_result.status());
  EXPECT_EQ(get_result.Unwrap()->value, 51);
}

}  // namespace aml_light
