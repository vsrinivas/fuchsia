// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-lp8556.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace ti {

constexpr uint32_t kMmioRegSize = sizeof(uint32_t);
constexpr uint32_t kMmioRegCount = (kAOBrightnessStickyReg + kMmioRegSize) / kMmioRegSize;

class Lp8556DeviceTest : public zxtest::Test {
 public:
  Lp8556DeviceTest()
      : mock_regs_(ddk_mock::MockMmioRegRegion(mock_reg_array_, kMmioRegSize, kMmioRegCount)) {}

  void SetUp() {
    ddk::MmioBuffer mmio(mock_regs_.GetMmioBuffer());
    mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();
    mock_i2c_.ExpectWrite({kCfg2Reg}).ExpectReadStop({kCfg2Default});

    fbl::AllocChecker ac;
    dev_ = fbl::make_unique_checked<Lp8556Device>(
        &ac, fake_ddk::kFakeParent, ddk::I2cChannel(mock_i2c_.GetProto()), std::move(mmio));
    EXPECT_TRUE(ac.check());

    ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
    ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
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

      uint16_t sticky_reg_value = static_cast<uint16_t>(brightness * kAOBrightnessStickyMaxValue);
      sticky_reg_value &= kAOBrightnessStickyMask;

      auto sticky_reg = BrightnessStickyReg::Get().FromValue(0);
      sticky_reg.set_brightness(sticky_reg_value);
      sticky_reg.set_is_valid(1);

      mock_regs_[BrightnessStickyReg::Get().addr()].ExpectWrite(sticky_reg.reg_value());
    }

    if (power != dev_->GetDevicePower()) {
      mock_i2c_.ExpectWriteStop({kDeviceControlReg, power ? kBacklightOn : kBacklightOff});
      if (power) {
        mock_i2c_.ExpectWriteStop({kCfg2Reg, dev_->GetCfg2()});
      }
    }
    EXPECT_OK(dev_->SetBacklightState(power, brightness));

    ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
    ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
  }

 private:
  mock_i2c::MockI2c mock_i2c_;
  std::unique_ptr<Lp8556Device> dev_;
  ddk_mock::MockMmioReg mock_reg_array_[kMmioRegCount];
  ddk_mock::MockMmioRegRegion mock_regs_;
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
