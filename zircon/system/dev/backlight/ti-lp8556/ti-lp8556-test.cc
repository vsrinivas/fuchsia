// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-lp8556.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

namespace ti {

class Lp8556DeviceTest : public zxtest::Test {
 public:
  Lp8556DeviceTest() {
    fbl::AllocChecker ac;
    dev_ = fbl::make_unique_checked<Lp8556Device>(&ac, fake_ddk::kFakeParent,
                                                  ddk::I2cChannel(mock_i2c_.GetProto()));
    EXPECT_TRUE(ac.check());
  }

  void TestLifecycle() {
    fake_ddk::Bind ddk;
    EXPECT_OK(dev_->DdkAdd("ti-lp8556"));
    dev_->DdkUnbind();
    EXPECT_TRUE(ddk.Ok());
    dev_->DdkRelease();
    __UNUSED auto ptr = dev_.release();
  }

  void VerifyGetBrightness(bool power, double brightness) {
    bool pwr;
    double brt;
    EXPECT_OK(dev_->GetBacklightState(&pwr, &brt));
    EXPECT_EQ(pwr, power);
    EXPECT_EQ(brt, brightness);
  }

  void VerifySetBrightness(bool power, double brightness) {
    if (brightness != dev_->GetDeviceBrightness()) {
      mock_i2c_.ExpectWriteStop(
          {kBacklightControlReg, static_cast<uint8_t>(brightness * kMaxBrightnessRegValue)});
    }
    if (power != dev_->GetDevicePower()) {
      mock_i2c_.ExpectWriteStop({kDeviceControlReg, power ? kBacklightOn : kBacklightOff});
      if (power) {
        mock_i2c_.ExpectWriteStop({kCfg2Reg, kCfg2Default});
      }
    }
    EXPECT_OK(dev_->SetBacklightState(power, brightness));

    ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
  }

 private:
  mock_i2c::MockI2c mock_i2c_;
  std::unique_ptr<Lp8556Device> dev_;
};

TEST_F(Lp8556DeviceTest, DdkLifecycle) { TestLifecycle(); }

TEST_F(Lp8556DeviceTest, Brightness) {
  VerifySetBrightness(false, 0.0);
  VerifyGetBrightness(false, 0.0);

  VerifySetBrightness(true, 0.5);
  VerifyGetBrightness(true, 0.5);

  VerifySetBrightness(true, 1.0);
  VerifyGetBrightness(true, 1.0);

  VerifySetBrightness(true, 0.0);
  VerifyGetBrightness(true, 0.0);
}

}  // namespace ti
