// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-lp8556.h"

#include <fuchsia/hardware/backlight/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <map>

#include <ddk/metadata.h>
#include <fbl/span.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace {

bool FloatNear(double a, double b) { return std::abs(a - b) < 0.001; }

}  // namespace

namespace ti {

constexpr uint32_t kMmioRegSize = sizeof(uint32_t);
constexpr uint32_t kMmioRegCount = (kAOBrightnessStickyReg + kMmioRegSize) / kMmioRegSize;

class Bind : fake_ddk::Bind {
 public:
  zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* data, size_t length,
                                size_t* actual) override {
    if (metadata_.find(type) == metadata_.end()) {
      return ZX_ERR_NOT_FOUND;
    }

    const fbl::Span<const uint8_t>& entry = metadata_[type];
    *actual = entry.size_bytes();
    memcpy(data, entry.data(), std::min(length, entry.size_bytes()));
    return ZX_OK;
  }

  void SetMetadata(uint32_t type, const void* data, size_t data_length) {
    metadata_[type] = fbl::Span(reinterpret_cast<const uint8_t*>(data), data_length);
  }

 private:
  std::map<uint32_t, fbl::Span<const uint8_t>> metadata_;
};

class Lp8556DeviceTest : public zxtest::Test {
 public:
  Lp8556DeviceTest()
      : mock_regs_(ddk_mock::MockMmioRegRegion(mock_reg_array_, kMmioRegSize, kMmioRegCount)),
        loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  void SetUp() {
    ddk::MmioBuffer mmio(mock_regs_.GetMmioBuffer());

    fbl::AllocChecker ac;
    dev_ = fbl::make_unique_checked<Lp8556Device>(
        &ac, fake_ddk::kFakeParent, ddk::I2cChannel(mock_i2c_.GetProto()), std::move(mmio));
    ASSERT_TRUE(ac.check());

    const auto message_op = [](void* ctx, fidl_incoming_msg_t* msg,
                               fidl_txn_t* txn) -> zx_status_t {
      return static_cast<Lp8556Device*>(ctx)->DdkMessage(msg, txn);
    };
    ASSERT_OK(messenger_.SetMessageOp(dev_.get(), message_op));
    ASSERT_OK(loop_.StartThread("lp8556-client-thread"));
  }

  void TestLifecycle() {
    fake_ddk::Bind ddk;
    EXPECT_OK(dev_->DdkAdd("ti-lp8556"));
    dev_->DdkAsyncRemove();
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
      uint16_t brightness_reg_value = static_cast<uint16_t>(brightness * kBrightnessRegMaxValue);
      mock_i2c_.ExpectWriteStop({kBacklightBrightnessLsbReg,
                                 static_cast<uint8_t>(brightness_reg_value & kBrightnessLsbMask)});
      // An I2C bus read is a write of the address followed by a read of the data.
      mock_i2c_.ExpectWrite({kBacklightBrightnessMsbReg}).ExpectReadStop({0});
      mock_i2c_.ExpectWriteStop(
          {kBacklightBrightnessMsbReg,
           static_cast<uint8_t>(
               ((brightness_reg_value & kBrightnessMsbMask) >> kBrightnessMsbShift) &
               kBrightnessMsbByteMask)});

      auto sticky_reg = BrightnessStickyReg::Get().FromValue(0);
      sticky_reg.set_brightness(brightness_reg_value & kBrightnessRegMask);
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

 protected:
  ::llcpp::fuchsia::hardware::backlight::Device::SyncClient client() {
    return ::llcpp::fuchsia::hardware::backlight::Device::SyncClient(std::move(messenger_.local()));
  }

  mock_i2c::MockI2c mock_i2c_;
  std::unique_ptr<Lp8556Device> dev_;
  ddk_mock::MockMmioRegRegion mock_regs_;

 private:
  ddk_mock::MockMmioReg mock_reg_array_[kMmioRegCount];
  fake_ddk::FidlMessenger messenger_;
  async::Loop loop_;
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

TEST_F(Lp8556DeviceTest, InitRegisters) {
  constexpr uint8_t kInitialRegisterValues[] = {
      0x01, 0x85, 0xa2, 0x30, 0xa3, 0x32, 0xa5, 0x54, 0xa7, 0xf4, 0xa9, 0x60, 0xae, 0x09,
  };

  Bind ddk;
  ddk.SetMetadata(DEVICE_METADATA_PRIVATE, kInitialRegisterValues, sizeof(kInitialRegisterValues));

  mock_i2c_.ExpectWriteStop({0x01, 0x85})
      .ExpectWriteStop({0xa2, 0x30})
      .ExpectWriteStop({0xa3, 0x32})
      .ExpectWriteStop({0xa5, 0x54})
      .ExpectWriteStop({0xa7, 0xf4})
      .ExpectWriteStop({0xa9, 0x60})
      .ExpectWriteStop({0xae, 0x09})
      .ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitNoRegisters) {
  Bind ddk;

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitInvalidRegisters) {
  constexpr uint8_t kInitialRegisterValues[] = {
      0x01, 0x85, 0xa2, 0x30, 0xa3, 0x32, 0xa5, 0x54, 0xa7, 0xf4, 0xa9, 0x60, 0xae,
  };

  Bind ddk;
  ddk.SetMetadata(DEVICE_METADATA_PRIVATE, kInitialRegisterValues, sizeof(kInitialRegisterValues));

  EXPECT_NOT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitTooManyRegisters) {
  constexpr uint8_t kInitialRegisterValues[514] = {};

  Bind ddk;
  ddk.SetMetadata(DEVICE_METADATA_PRIVATE, kInitialRegisterValues, sizeof(kInitialRegisterValues));

  EXPECT_NOT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitOverwriteBrightnessRegisters) {
  constexpr uint8_t kInitialRegisterValues[] = {
      kBacklightBrightnessLsbReg,
      0xab,
      kBacklightBrightnessMsbReg,
      0xcd,
  };

  Bind ddk;
  ddk.SetMetadata(DEVICE_METADATA_PRIVATE, kInitialRegisterValues, sizeof(kInitialRegisterValues));

  mock_i2c_.ExpectWriteStop({kBacklightBrightnessLsbReg, 0xab})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0xcd})
      .ExpectWriteStop({kBacklightBrightnessLsbReg, 0x00})
      .ExpectWrite({kBacklightBrightnessMsbReg})
      .ExpectReadStop({0xcd})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0xc4})
      .ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e});

  const uint32_t kStickyRegValue =
      BrightnessStickyReg::Get().FromValue(0).set_is_valid(1).set_brightness(0x400).reg_value();
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead(kStickyRegValue);

  EXPECT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, ReadDefaultCurrentScale) {
  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ::llcpp::fuchsia::hardware::backlight::Device::SyncClient backlight_client(client());
  auto result = backlight_client.GetNormalizedBrightnessScale();
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result.value().result.is_err());
  EXPECT_TRUE(
      FloatNear(result.value().result.response().scale, static_cast<double>(0xe05) / 0xfff));

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, SetCurrentScale) {
  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ::llcpp::fuchsia::hardware::backlight::Device::SyncClient backlight_client(client());

  mock_i2c_.ExpectWrite({kCurrentMsbReg})
      .ExpectReadStop({0x7e})
      .ExpectWriteStop({kCurrentLsbReg, 0xab, 0x72});

  auto set_result =
      backlight_client.SetNormalizedBrightnessScale(static_cast<double>(0x2ab) / 0xfff);
  EXPECT_TRUE(set_result.ok());
  EXPECT_FALSE(set_result.value().result.is_err());

  auto get_result = backlight_client.GetNormalizedBrightnessScale();
  EXPECT_TRUE(get_result.ok());
  EXPECT_FALSE(get_result.value().result.is_err());
  EXPECT_TRUE(
      FloatNear(get_result.value().result.response().scale, static_cast<double>(0x2ab) / 0xfff));

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, SetAbsoluteBrightnessScaleReset) {
  Bind ddk;

  constexpr double kMaxBrightnessInNits = 350.0;
  ddk.SetMetadata(DEVICE_METADATA_BACKLIGHT_MAX_BRIGHTNESS_NITS, &kMaxBrightnessInNits,
                  sizeof(kMaxBrightnessInNits));

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ::llcpp::fuchsia::hardware::backlight::Device::SyncClient backlight_client(client());

  mock_i2c_.ExpectWrite({kCurrentMsbReg})
      .ExpectReadStop({0x7e})
      .ExpectWriteStop({kCurrentLsbReg, 0xab, 0x72});

  auto set_result =
      backlight_client.SetNormalizedBrightnessScale(static_cast<double>(0x2ab) / 0xfff);
  EXPECT_TRUE(set_result.ok());
  EXPECT_FALSE(set_result.value().result.is_err());

  mock_i2c_.ExpectWrite({kCurrentMsbReg})
      .ExpectReadStop({0x6e})
      .ExpectWriteStop({kCurrentLsbReg, 0x05, 0x6e})
      .ExpectWriteStop({kBacklightBrightnessLsbReg, 0xff})
      .ExpectWrite({kBacklightBrightnessMsbReg})
      .ExpectReadStop({0xab})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0xa7});

  auto absolute_result_1 = backlight_client.SetStateAbsolute({true, 175.0});
  EXPECT_TRUE(absolute_result_1.ok());
  EXPECT_FALSE(absolute_result_1.value().result.is_err());

  // The scale is already set to the default, so the register should not be written again.
  mock_i2c_.ExpectWriteStop({kBacklightBrightnessLsbReg, 0xff})
      .ExpectWrite({kBacklightBrightnessMsbReg})
      .ExpectReadStop({0x1b})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0x13});

  auto absolute_result_2 = backlight_client.SetStateAbsolute({true, 87.5});
  EXPECT_TRUE(absolute_result_2.ok());
  EXPECT_FALSE(absolute_result_2.value().result.is_err());

  ASSERT_NO_FATAL_FAILURES(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_i2c_.VerifyAndClear());
}

}  // namespace ti
