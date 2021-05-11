// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tmp112.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

}  // namespace

namespace temperature {

using TemperatureClient = fidl::WireSyncClient<fuchsia_hardware_temperature::Device>;

class Tmp112DeviceTest : public zxtest::Test {
 public:
  Tmp112DeviceTest() {}

  void SetUp() override {
    dev_ = std::make_unique<Tmp112Device>(fake_ddk::kFakeParent,
                                          ddk::I2cChannel(mock_i2c_.GetProto()));

    const auto message_op = [](void* ctx, fidl_incoming_msg_t* msg,
                               fidl_txn_t* txn) -> zx_status_t {
      return static_cast<Tmp112Device*>(ctx)->ddk_device_proto_.message(ctx, msg, txn);
    };
    ASSERT_OK(messenger_.SetMessageOp(dev_.get(), message_op));
  }

 protected:
  mock_i2c::MockI2c mock_i2c_;
  std::unique_ptr<Tmp112Device> dev_;
  fake_ddk::FidlMessenger messenger_;
};

TEST_F(Tmp112DeviceTest, Init) {
  uint8_t initial_config_val[2] = {kConfigConvertResolutionSet12Bit, 0};
  mock_i2c_.ExpectWrite({kConfigReg}).ExpectReadStop({0x0, 0x0});
  mock_i2c_.ExpectWriteStop({kConfigReg, initial_config_val[0], initial_config_val[1]});
  dev_->Init();

  mock_i2c_.VerifyAndClear();
}

TEST_F(Tmp112DeviceTest, GetTemperatureCelsius) {
  mock_i2c_.ExpectWrite({kTemperatureReg}).ExpectReadStop({0x34, 0x12});

  TemperatureClient client(std::move(messenger_.local()));
  auto result = client.GetTemperatureCelsius();
  EXPECT_OK(result->status);
  EXPECT_TRUE(FloatNear(result->temp, dev_->RegToTemperatureCelsius(0x1234)));

  mock_i2c_.VerifyAndClear();
}

TEST_F(Tmp112DeviceTest, RegToTemperature) {
  EXPECT_TRUE(FloatNear(dev_->RegToTemperatureCelsius(0x1234), 52.0625));
}

}  // namespace temperature
