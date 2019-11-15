// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pwm.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace pwm {

zx_status_t fake_set_config(void* ctx, uint32_t idx, const pwm_config_t* config) { return ZX_OK; }
zx_status_t fake_enable(void* ctx, uint32_t idx) { return ZX_OK; }
zx_status_t fake_disable(void* ctx, uint32_t idx) { return ZX_OK; }

pwm_impl_protocol_ops_t fake_ops{
    .set_config = &fake_set_config,
    .enable = &fake_enable,
    .disable = &fake_disable,
};

pwm_impl_protocol_t fake_proto{.ops = &fake_ops, .ctx = nullptr};

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

TEST_F(PwmDeviceTest, GetConfigTest) { pwm_->PwmGetConfig(nullptr); }

TEST_F(PwmDeviceTest, SetConfigTest) {
  EXPECT_EQ(pwm_->PwmSetConfig(nullptr), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PwmDeviceTest, EnableTest) { EXPECT_EQ(pwm_->PwmEnable(), ZX_ERR_NOT_SUPPORTED); }

TEST_F(PwmDeviceTest, DisableTest) { EXPECT_EQ(pwm_->PwmDisable(), ZX_ERR_NOT_SUPPORTED); }

}  // namespace pwm
