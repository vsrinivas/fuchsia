// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/sync/completion.h>

#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace audio {

struct Max98373Test : public Max98373 {
  explicit Max98373Test(const ddk::I2cChannel& i2c, const ddk::GpioProtocolClient& codec_reset)
      : Max98373(fake_ddk::kFakeParent, i2c, codec_reset) {
    initialized_ = true;
  }
  zx_status_t SoftwareResetAndInitialize() { return Max98373::SoftwareResetAndInitialize(); }
  zx_status_t HardwareReset() { return Max98373::HardwareReset(); }
};

TEST(Max98373Test, GetInfo) {
  ddk::I2cChannel unused_i2c;
  ddk::GpioProtocolClient unused_gpio;
  Max98373 device(nullptr, std::move(unused_i2c), std::move(unused_gpio));

  device.CodecGetInfo(
      [](void* ctx, const info_t* info) {
        EXPECT_EQ(strcmp(info->unique_id, ""), 0);
        EXPECT_EQ(strcmp(info->manufacturer, "Maxim"), 0);
        EXPECT_EQ(strcmp(info->product_name, "MAX98373"), 0);
      },
      nullptr);
}

TEST(Max98373Test, HardwareReset) {
  ddk::I2cChannel unused_i2c;
  ddk::MockGpio mock_gpio;
  ddk::GpioProtocolClient gpio(mock_gpio.GetProto());

  mock_gpio.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Reset, set to 0 and then to 1.
  Max98373Test device(std::move(unused_i2c), std::move(gpio));
  device.HardwareReset();
  mock_gpio.VerifyAndClear();
}

TEST(Max98373Test, SotfwareResetAndInitialize) {
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio;

  mock_i2c
      .ExpectWriteStop({0x20, 0x00, 0x01})  // Reset.
      .ExpectWrite({0x21, 0xff})
      .ExpectReadStop({0x43})                // Get revision id.
      .ExpectWriteStop({0x20, 0xff, 0x01})   // Global enable.
      .ExpectWriteStop({0x20, 0x43, 0x01})   // Speaker enable.
      .ExpectWriteStop({0x20, 0x3d, 0x28})   // Set gain to -20dB.
      .ExpectWriteStop({0x20, 0x2b, 0x01})   // Data in enable.
      .ExpectWriteStop({0x20, 0x24, 0xc0})   // I2S.
      .ExpectWriteStop({0x20, 0x27, 0x08});  // 48KHz.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Max98373Test device(std::move(i2c), std::move(unused_gpio));
  device.Bind();
  // Delay to test we don't do other init I2C writes in another thread.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  EXPECT_OK(device.SoftwareResetAndInitialize());
  mock_i2c.VerifyAndClear();
}

TEST(Max98373Test, SetGainGood) {
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio;

  mock_i2c.ExpectWriteStop({0x20, 0x3d, 0x40});  // -32dB.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Max98373Test device(std::move(i2c), std::move(unused_gpio));
  gain_state_t gain_state = {};
  gain_state.gain = -32.f;
  device.CodecSetGainState(
      &gain_state, [](void* cookie) { EXPECT_EQ(cookie, nullptr); }, nullptr);
  mock_i2c.VerifyAndClear();
}

TEST(Max98373Test, SetGainOurOfRangeLow) {
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio;

  mock_i2c.ExpectWriteStop({0x20, 0x3d, 0x7f});  // -63.5dB.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Max98373Test device(std::move(i2c), std::move(unused_gpio));
  gain_state_t gain_state = {};
  gain_state.gain = -999.f;
  device.CodecSetGainState(
      &gain_state, [](void* cookie) { EXPECT_EQ(cookie, nullptr); }, nullptr);

  mock_i2c.VerifyAndClear();
}

TEST(Max98373Test, SetGainOurOfRangeHigh) {
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio;

  mock_i2c.ExpectWriteStop({0x20, 0x3d, 0x00});  // 0dB.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Max98373Test device(std::move(i2c), std::move(unused_gpio));
  gain_state_t gain_state = {};
  gain_state.gain = 999.f;
  device.CodecSetGainState(
      &gain_state, [](void* cookie) { EXPECT_EQ(cookie, nullptr); }, nullptr);

  mock_i2c.VerifyAndClear();
}

}  // namespace audio
