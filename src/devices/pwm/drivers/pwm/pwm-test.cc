// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pwm.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace pwm {

zx_status_t fake_get_config(void* ctx, uint32_t idx, pwm_config_t* out_config) { return ZX_OK; }
zx_status_t fake_set_config(void* ctx, uint32_t idx, const pwm_config_t* config) { return ZX_OK; }
zx_status_t fake_enable(void* ctx, uint32_t idx) { return ZX_OK; }
zx_status_t fake_disable(void* ctx, uint32_t idx) { return ZX_OK; }

pwm_impl_protocol_ops_t fake_ops{
    .get_config = &fake_get_config,
    .set_config = &fake_set_config,
    .enable = &fake_enable,
    .disable = &fake_disable,
};

pwm_impl_protocol_t fake_proto{.ops = &fake_ops, .ctx = nullptr};

struct fake_mode_config {
  uint32_t mode;
};

class FakePwmDevice : public PwmDevice {
 public:
  static std::unique_ptr<FakePwmDevice> Create() {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakePwmDevice>(&ac);
    if (!ac.check()) {
      return nullptr;
    }

    return device;
  }

  explicit FakePwmDevice() : PwmDevice(&fake_proto) {}
};

class PwmDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    pwm_ = FakePwmDevice::Create();
    ASSERT_NOT_NULL(pwm_);
  }

  void TearDown() override {}

 protected:
  std::unique_ptr<FakePwmDevice> pwm_;
};

TEST_F(PwmDeviceTest, GetConfigTest) {
  pwm_config_t fake_config = {
      false, 0, 0.0, nullptr, 0,
  };
  EXPECT_OK(pwm_->PwmGetConfig(&fake_config));
  EXPECT_OK(pwm_->PwmGetConfig(&fake_config));  // Second time
}

TEST_F(PwmDeviceTest, SetConfigTest) {
  fake_mode_config fake_mode{
      .mode = 0,
  };
  pwm_config_t fake_config{
      .polarity = false,
      .period_ns = 1000,
      .duty_cycle = 45.0,
      .mode_config_buffer = &fake_mode,
      .mode_config_size = sizeof(fake_mode),
  };
  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));

  fake_mode.mode = 3;
  fake_config.polarity = true;
  fake_config.duty_cycle = 68.0;
  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));

  EXPECT_OK(pwm_->PwmSetConfig(&fake_config));
}

TEST_F(PwmDeviceTest, EnableTest) {
  EXPECT_OK(pwm_->PwmEnable());
  EXPECT_OK(pwm_->PwmEnable());  // Second time
}

TEST_F(PwmDeviceTest, DisableTest) {
  EXPECT_OK(pwm_->PwmDisable());
  EXPECT_OK(pwm_->PwmDisable());  // Second time
}

}  // namespace pwm
