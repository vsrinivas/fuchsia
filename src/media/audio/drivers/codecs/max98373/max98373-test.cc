// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace audio {

class Max98373Test : public zxtest::Test {
 public:
  void SetUp() override {
    // Reset by the TAS driver initialization.
    mock_i2c_
        .ExpectWriteStop({0x20, 0x00, 0x01}, ZX_ERR_INTERNAL)  // Reset, error will retry.
        .ExpectWriteStop({0x20, 0x00, 0x01}, ZX_ERR_INTERNAL)  // Reset, error will retry.
        .ExpectWriteStop({0x20, 0x00, 0x01})                   // Reset.
        .ExpectWrite({0x21, 0xff})
        .ExpectReadStop({0x43})                // Get revision id.
        .ExpectWriteStop({0x20, 0xff, 0x01})   // Global enable.
        .ExpectWriteStop({0x20, 0x43, 0x01})   // Speaker enable.
        .ExpectWriteStop({0x20, 0x3d, 0x28})   // Set gain to -20dB.
        .ExpectWriteStop({0x20, 0x2b, 0x01})   // Data in enable.
        .ExpectWriteStop({0x20, 0x26, 0x08})   // 256 ratio.
        .ExpectWriteStop({0x20, 0x24, 0xd8})   // TDM.
        .ExpectWriteStop({0x20, 0x27, 0x08});  // 48KHz.
  }
  mock_i2c::MockI2c mock_i2c_;
};

struct Max98373Codec : public Max98373 {
  explicit Max98373Codec(const ddk::I2cChannel& i2c, const ddk::GpioProtocolClient& codec_reset,
                         zx_device_t* parent)
      : Max98373(parent, i2c, codec_reset) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

TEST_F(Max98373Test, GetInfo) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  ddk::GpioProtocolClient unused_gpio;
  auto codec = SimpleCodecServer::Create<Max98373Codec>(mock_i2c_.GetProto(),
                                                        std::move(unused_gpio), fake_parent.get());
  ASSERT_NOT_NULL(codec);
  // TODO(fxbug.dev/82160): SimpleCodecServer::Create should not return a unique_ptr to an object it
  // does not own.  We release it now, because we don't own it.
  // Recommended way to get the codec_ptr:
  // auto codec_ptr = fake_parent.GetLatestChild()->GetDeviceContext<Max98373Codec>();
  auto codec_ptr = codec.release();
  auto codec_proto = codec_ptr->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    auto info = client.GetInfo();
    EXPECT_EQ(info.value().unique_id.compare(""), 0);
    EXPECT_EQ(info.value().manufacturer.compare("Maxim"), 0);
    EXPECT_EQ(info.value().product_name.compare("MAX98373"), 0);
  }
}

TEST_F(Max98373Test, Reset) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();

  mock_i2c_
      .ExpectWriteStop({0x20, 0x00, 0x01}, ZX_ERR_INTERNAL)  // Reset, error will retry.
      .ExpectWriteStop({0x20, 0x00, 0x01}, ZX_ERR_INTERNAL)  // Reset, error will retry.
      .ExpectWriteStop({0x20, 0x00, 0x01})                   // Reset.
      .ExpectWrite({0x21, 0xff})
      .ExpectReadStop({0x43})                // Get revision id.
      .ExpectWriteStop({0x20, 0xff, 0x01})   // Global enable.
      .ExpectWriteStop({0x20, 0x43, 0x01})   // Speaker enable.
      .ExpectWriteStop({0x20, 0x3d, 0x28})   // Set gain to -20dB.
      .ExpectWriteStop({0x20, 0x2b, 0x01})   // Data in enable.
      .ExpectWriteStop({0x20, 0x26, 0x08})   // 256 ratio.
      .ExpectWriteStop({0x20, 0x24, 0xd8})   // TDM.
      .ExpectWriteStop({0x20, 0x27, 0x08});  // 48KHz.

  ddk::MockGpio mock_gpio;
  mock_gpio.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Reset, set to 0 and then to 1.
  ddk::GpioProtocolClient gpio(mock_gpio.GetProto());
  auto codec = SimpleCodecServer::Create<Max98373Codec>(mock_i2c_.GetProto(), std::move(gpio),
                                                        fake_parent.get());
  ASSERT_NOT_NULL(codec);
  // TODO(fxbug.dev/82160): SimpleCodecServer::Create should not return a unique_ptr to an object it
  // does not own.  We release it now, because we don't own it.
  auto codec_ptr = codec.release();
  auto codec_proto = codec_ptr->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // Delay to test we don't do other init I2C writes in another thread.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  ASSERT_OK(client.Reset());

  mock_i2c_.VerifyAndClear();
  mock_gpio.VerifyAndClear();
}

TEST_F(Max98373Test, SetGainGood) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();

  mock_i2c_.ExpectWriteStop({0x20, 0x3d, 0x40});  // -32dB.

  ddk::GpioProtocolClient unused_gpio;
  auto codec = SimpleCodecServer::Create<Max98373Codec>(mock_i2c_.GetProto(),
                                                        std::move(unused_gpio), fake_parent.get());
  ASSERT_NOT_NULL(codec);
  // TODO(fxbug.dev/82160): SimpleCodecServer::Create should not return a unique_ptr to an object it
  // does not own.  We release it now, because we don't own it.
  auto codec_ptr = codec.release();
  auto codec_proto = codec_ptr->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  GainState gain({.gain = -32.f, .muted = false, .agc_enabled = false});
  client.SetGainState(gain);

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  auto unused = client.GetInfo();
  static_cast<void>(unused);

  mock_i2c_.VerifyAndClear();
}

TEST_F(Max98373Test, SetGainOurOfRangeLow) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();

  mock_i2c_.ExpectWriteStop({0x20, 0x3d, 0x7f});  // -63.5dB.

  ddk::GpioProtocolClient unused_gpio;
  auto codec = SimpleCodecServer::Create<Max98373Codec>(mock_i2c_.GetProto(),
                                                        std::move(unused_gpio), fake_parent.get());
  ASSERT_NOT_NULL(codec);
  // TODO(fxbug.dev/82160): SimpleCodecServer::Create should not return a unique_ptr to an object it
  // does not own.  We release it now, because we don't own it.
  auto codec_ptr = codec.release();
  auto codec_proto = codec_ptr->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  GainState gain({.gain = -999.f, .muted = false, .agc_enabled = false});
  client.SetGainState(gain);

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  auto unused = client.GetInfo();
  static_cast<void>(unused);

  mock_i2c_.VerifyAndClear();
}

TEST_F(Max98373Test, SetGainOurOfRangeHigh) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();

  mock_i2c_.ExpectWriteStop({0x20, 0x3d, 0x00});  // 0dB.

  ddk::GpioProtocolClient unused_gpio;
  auto codec = SimpleCodecServer::Create<Max98373Codec>(mock_i2c_.GetProto(),
                                                        std::move(unused_gpio), fake_parent.get());
  ASSERT_NOT_NULL(codec);
  // TODO(fxbug.dev/82160): SimpleCodecServer::Create should not return a unique_ptr to an object it
  // does not own.  We release it now, because we don't own it.
  auto codec_ptr = codec.release();
  auto codec_proto = codec_ptr->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  GainState gain({.gain = 999.f, .muted = false, .agc_enabled = false});
  client.SetGainState(gain);

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  auto unused = client.GetInfo();
  static_cast<void>(unused);

  mock_i2c_.VerifyAndClear();
}

}  // namespace audio
