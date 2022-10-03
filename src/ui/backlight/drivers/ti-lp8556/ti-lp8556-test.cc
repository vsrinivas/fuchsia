// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-lp8556.h"

#include <fidl/fuchsia.hardware.backlight/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.sensor/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/metadata.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/stdcompat/span.h>
#include <math.h>

#include <map>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "sdk/lib/inspect/testing/cpp/zxtest/inspect.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

bool FloatNear(double a, double b) { return std::abs(a - b) < 0.001; }

}  // namespace

namespace ti {

constexpr uint32_t kMmioRegSize = sizeof(uint32_t);
constexpr uint32_t kMmioRegCount = (kAOBrightnessStickyReg + kMmioRegSize) / kMmioRegSize;

class Lp8556DeviceTest : public zxtest::Test, public inspect::InspectTestHelper {
 public:
  Lp8556DeviceTest()
      : mock_regs_(ddk_mock::MockMmioRegRegion(mock_reg_array_, kMmioRegSize, kMmioRegCount)),
        fake_parent_(MockDevice::FakeRootParent()),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        i2c_loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override {
    fdf::MmioBuffer mmio(mock_regs_.GetMmioBuffer());

    auto i2c_endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    fidl::BindServer(i2c_loop_.dispatcher(), std::move(i2c_endpoints->server), &mock_i2c_);

    fbl::AllocChecker ac;
    dev_ = fbl::make_unique_checked<Lp8556Device>(
        &ac, fake_parent_.get(), std::move(i2c_endpoints->client), std::move(mmio));
    ASSERT_TRUE(ac.check());

    auto backlight_endpoints = fidl::CreateEndpoints<fuchsia_hardware_backlight::Device>();
    fidl::BindServer(
        loop_.dispatcher(), std::move(backlight_endpoints->server),
        static_cast<fidl::WireServer<fuchsia_hardware_backlight::Device>*>(dev_.get()));
    backlight_client_ = std::move(backlight_endpoints->client);

    auto power_sensor_endpoints = fidl::CreateEndpoints<fuchsia_hardware_power_sensor::Device>();
    fidl::BindServer(
        loop_.dispatcher(), std::move(power_sensor_endpoints->server),
        static_cast<fidl::WireServer<fuchsia_hardware_power_sensor::Device>*>(dev_.get()));
    power_sensor_client_ = std::move(power_sensor_endpoints->client);

    ASSERT_OK(loop_.StartThread("lp8556-client-thread"));
    ASSERT_OK(i2c_loop_.StartThread("mock-i2c-driver-thread"));
  }

  void TestLifecycle() {
    EXPECT_OK(dev_->DdkAdd("ti-lp8556"));
    EXPECT_EQ(fake_parent_->child_count(), 1);
    dev_->DdkAsyncRemove();
    EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(fake_parent_.get()));  // Calls DdkRelease() on dev_.
    __UNUSED auto ptr = dev_.release();
    EXPECT_EQ(fake_parent_->child_count(), 0);
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
      uint16_t brightness_reg_value =
          static_cast<uint16_t>(ceil(brightness * kBrightnessRegMaxValue));
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
      const uint8_t control_value = kDeviceControlDefaultValue | (power ? kBacklightOn : 0);
      mock_i2c_.ExpectWriteStop({kDeviceControlReg, control_value});
      if (power) {
        mock_i2c_.ExpectWriteStop({kCfg2Reg, dev_->GetCfg2()});
      }
    }
    EXPECT_OK(dev_->SetBacklightState(power, brightness));

    ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
    ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
  }

 protected:
  fidl::WireSyncClient<fuchsia_hardware_backlight::Device> client() {
    return fidl::WireSyncClient<fuchsia_hardware_backlight::Device>(std::move(backlight_client_));
  }

  fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device> sensorSyncClient() {
    return fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device>(
        std::move(power_sensor_client_));
  }

  mock_i2c::MockI2c mock_i2c_;
  std::unique_ptr<Lp8556Device> dev_;
  ddk_mock::MockMmioRegRegion mock_regs_;
  std::shared_ptr<MockDevice> fake_parent_;

 private:
  ddk_mock::MockMmioReg mock_reg_array_[kMmioRegCount];
  fidl::ClientEnd<fuchsia_hardware_backlight::Device> backlight_client_;
  fidl::ClientEnd<fuchsia_hardware_power_sensor::Device> power_sensor_client_;
  async::Loop loop_;
  async::Loop i2c_loop_;
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
  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 0,
      .registers =
          {
              // Registers
              0x01, 0x85,  // Device Control
                           // EPROM
              0xa2, 0x30,  // CFG2
              0xa3, 0x32,  // CFG3
              0xa5, 0x54,  // CFG5
              0xa7, 0xf4,  // CFG7
              0xa9, 0x60,  // CFG9
              0xae, 0x09,  // CFGE
          },
      .register_count = 14,
  };
  // constexpr uint8_t kInitialRegisterValues[] = {
  //     0x01, 0x85, 0xa2, 0x30, 0xa3, 0x32, 0xa5, 0x54, 0xa7, 0xf4, 0xa9, 0x60, 0xae, 0x09,
  // };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

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
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitNoRegisters) {
  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitInvalidRegisters) {
  constexpr uint8_t kInitialRegisterValues[] = {
      0x01, 0x85, 0xa2, 0x30, 0xa3, 0x32, 0xa5, 0x54, 0xa7, 0xf4, 0xa9, 0x60, 0xae,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, kInitialRegisterValues,
                            sizeof(kInitialRegisterValues));

  EXPECT_NOT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, InitTooManyRegisters) {
  constexpr uint8_t kInitialRegisterValues[514] = {};

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, kInitialRegisterValues,
                            sizeof(kInitialRegisterValues));

  EXPECT_NOT_OK(dev_->Init());

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, OverwriteStickyRegister) {
  // constexpr uint8_t kInitialRegisterValues[] = {
  //     kBacklightBrightnessLsbReg,
  //     0xab,
  //     kBacklightBrightnessMsbReg,
  //     0xcd,
  // };

  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 0,
      .registers =
          {// Registers
           kBacklightBrightnessLsbReg, 0xab, kBacklightBrightnessMsbReg, 0xcd},
      .register_count = 4,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

  mock_i2c_.ExpectWriteStop({kBacklightBrightnessLsbReg, 0xab})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0xcd})
      .ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0xcd})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  const uint32_t kStickyRegValue =
      BrightnessStickyReg::Get().FromValue(0).set_is_valid(1).set_brightness(0x400).reg_value();
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectWrite(kStickyRegValue);

  // The DUT should set the brightness to 0.25 by writing 0x0400, starting with the LSB. The MSB
  // register needs to be RMW, so check that the upper four bits are preserved (0xab -> 0xa4).
  mock_i2c_.ExpectWriteStop({kBacklightBrightnessLsbReg, 0x00})
      .ExpectWrite({kBacklightBrightnessMsbReg})
      .ExpectReadStop({0xab})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0xa4});

  fidl::WireSyncClient<fuchsia_hardware_backlight::Device> backlight_client(client());

  auto result = backlight_client->SetStateNormalized({true, 0.25});
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result->is_error());

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, ReadDefaultCurrentScale) {
  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 0,
      .allow_set_current_scale = true,
      .register_count = 0,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  fidl::WireSyncClient<fuchsia_hardware_backlight::Device> backlight_client(client());
  auto result = backlight_client->GetNormalizedBrightnessScale();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_ok());
  EXPECT_TRUE(FloatNear(result->value()->scale, static_cast<double>(0xe05) / 0xfff));

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, SetCurrentScale) {
  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 0,
      .allow_set_current_scale = true,
      .register_count = 0,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  fidl::WireSyncClient<fuchsia_hardware_backlight::Device> backlight_client(client());

  mock_i2c_.ExpectWrite({kCfgReg}).ExpectReadStop({0x7e}).ExpectWriteStop(
      {kCurrentLsbReg, 0xab, 0x72});

  auto set_result =
      backlight_client->SetNormalizedBrightnessScale(static_cast<double>(0x2ab) / 0xfff);
  ASSERT_TRUE(set_result.ok());
  EXPECT_TRUE(set_result->is_ok());

  auto get_result = backlight_client->GetNormalizedBrightnessScale();
  ASSERT_TRUE(get_result.ok());
  ASSERT_TRUE(get_result->is_ok());
  EXPECT_TRUE(FloatNear(get_result->value()->scale, static_cast<double>(0x2ab) / 0xfff));

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, SetAbsoluteBrightnessScaleReset) {
  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 0,
      .allow_set_current_scale = true,
      .register_count = 0,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

  constexpr double kMaxBrightnessInNits = 350.0;
  fake_parent_->SetMetadata(DEVICE_METADATA_BACKLIGHT_MAX_BRIGHTNESS_NITS, &kMaxBrightnessInNits,
                            sizeof(kMaxBrightnessInNits));

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  fidl::WireSyncClient<fuchsia_hardware_backlight::Device> backlight_client(client());

  mock_i2c_.ExpectWrite({kCfgReg}).ExpectReadStop({0x7e}).ExpectWriteStop(
      {kCurrentLsbReg, 0xab, 0x72});

  auto set_result =
      backlight_client->SetNormalizedBrightnessScale(static_cast<double>(0x2ab) / 0xfff);
  EXPECT_TRUE(set_result.ok());
  EXPECT_FALSE(set_result->is_error());

  mock_i2c_.ExpectWrite({kCfgReg})
      .ExpectReadStop({0x6e})
      .ExpectWriteStop({kCurrentLsbReg, 0x05, 0x6e})
      .ExpectWriteStop({kBacklightBrightnessLsbReg, 0x00})
      .ExpectWrite({kBacklightBrightnessMsbReg})
      .ExpectReadStop({0xab})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0xa8});

  auto absolute_result_1 = backlight_client->SetStateAbsolute({true, 175.0});
  EXPECT_TRUE(absolute_result_1.ok());
  EXPECT_FALSE(absolute_result_1->is_error());

  // The scale is already set to the default, so the register should not be written again.
  mock_i2c_.ExpectWriteStop({kBacklightBrightnessLsbReg, 0x00})
      .ExpectWrite({kBacklightBrightnessMsbReg})
      .ExpectReadStop({0x1b})
      .ExpectWriteStop({kBacklightBrightnessMsbReg, 0x14});

  auto absolute_result_2 = backlight_client->SetStateAbsolute({true, 87.5});
  EXPECT_TRUE(absolute_result_2.ok());
  EXPECT_FALSE(absolute_result_2->is_error());

  ASSERT_NO_FATAL_FAILURE(mock_regs_[BrightnessStickyReg::Get().addr()].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURE(mock_i2c_.VerifyAndClear());
}

TEST_F(Lp8556DeviceTest, Inspect) {
  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x05, 0x4e})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xff, 0x0f})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x01});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  ReadInspect(dev_->InspectVmo());
  auto& root_node = hierarchy().GetByPath({"ti-lp8556"})->node();
  CheckProperty(root_node, "brightness", inspect::DoublePropertyValue(1.0));

  EXPECT_FALSE(root_node.get_property<inspect::UintPropertyValue>("persistent_brightness"));
  CheckProperty(root_node, "scale", inspect::UintPropertyValue(3589u));
  CheckProperty(root_node, "calibrated_scale", inspect::UintPropertyValue(3589u));
  CheckProperty(root_node, "power", inspect::BoolPropertyValue(true));
  EXPECT_FALSE(
      root_node.get_property<inspect::DoublePropertyValue>("max_absolute_brightness_nits"));
}

TEST_F(Lp8556DeviceTest, GetBackLightPower) {
  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 2,
      .registers = {},
      .register_count = 0,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x42, 0x36})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x36});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  VerifySetBrightness(false, 0.0);
  EXPECT_LT(abs(dev_->GetBacklightPower(0) - 0.0141694967), 0.000001f);

  VerifySetBrightness(true, 0.5);
  EXPECT_LT(abs(dev_->GetBacklightPower(2048) - 0.5352831254), 0.000001f);

  VerifySetBrightness(true, 1.0);
  EXPECT_LT(abs(dev_->GetBacklightPower(4095) - 1.0637770353), 0.000001f);
}

TEST_F(Lp8556DeviceTest, GetPowerWatts) {
  TiLp8556Metadata kDeviceMetadata = {
      .panel_id = 2,
      .registers = {},
      .register_count = 0,
  };

  fake_parent_->AddProtocol(ZX_PROTOCOL_PDEV, nullptr, nullptr, "pdev");
  fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kDeviceMetadata, sizeof(kDeviceMetadata));

  mock_i2c_.ExpectWrite({kCfg2Reg})
      .ExpectReadStop({kCfg2Default})
      .ExpectWrite({kCurrentLsbReg})
      .ExpectReadStop({0x42, 0x36})
      .ExpectWrite({kBacklightBrightnessLsbReg})
      .ExpectReadStop({0xab, 0x05})
      .ExpectWrite({kDeviceControlReg})
      .ExpectReadStop({0x85})
      .ExpectWrite({kCfgReg})
      .ExpectReadStop({0x36});
  mock_regs_[BrightnessStickyReg::Get().addr()].ExpectRead();

  EXPECT_OK(dev_->Init());

  VerifySetBrightness(true, 1.0);
  EXPECT_LT(abs(dev_->GetBacklightPower(4095) - 1.0637770353), 0.000001f);

  fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device> sensor_client(sensorSyncClient());
  auto result = sensor_client->GetPowerWatts();
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result->is_error());
}

}  // namespace ti
