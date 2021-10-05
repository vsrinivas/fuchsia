// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5782.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace audio {

namespace {

audio::DaiFormat GetDefaultDaiFormat() {
  return {
      .number_of_channels = 2,
      .channels_to_use_bitmask = 3,
      .sample_format = SampleFormat::PCM_SIGNED,
      .frame_format = FrameFormat::I2S,
      .frame_rate = 48'000,
      .bits_per_slot = 32,
      .bits_per_sample = 32,
  };
}
}  // namespace

struct Tas5782Codec : public Tas5782 {
  explicit Tas5782Codec(zx_device_t* parent, const ddk::I2cChannel& i2c,
                        const ddk::GpioProtocolClient& codec_reset,
                        const ddk::GpioProtocolClient& codec_mute)
      : Tas5782(parent, i2c, codec_reset, codec_mute) {
    initialized_ = true;
  }
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

TEST(Tas5782Test, GoodSetDai) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(
      fake_parent.get(), mock_i2c.GetProto(), std::move(unused_gpio0), std::move(unused_gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  DaiFormat format = GetDefaultDaiFormat();
  ASSERT_OK(client.SetDaiFormat(std::move(format)));

  mock_i2c.VerifyAndClear();
}

TEST(Tas5782Test, BadSetDai) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(
      fake_parent.get(), mock_i2c.GetProto(), std::move(unused_gpio0), std::move(unused_gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_format = FrameFormat::STEREO_LEFT;  // This must fail, only I2S supported.
    zx::status<CodecFormatInfo> codec_state = client.SetDaiFormat(std::move(format));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, codec_state.status_value());
  }
  {
    DaiFormat format = GetDefaultDaiFormat();
    format.number_of_channels = 1;  // Almost good format (wrong channels).
    zx::status<CodecFormatInfo> codec_state = client.SetDaiFormat(std::move(format));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, codec_state.status_value());
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas5782Test, GetDai) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c mock_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(
      fake_parent.get(), mock_i2c.GetProto(), std::move(unused_gpio0), std::move(unused_gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  auto formats = client.GetDaiFormats();

  EXPECT_EQ(formats.value().number_of_channels.size(), 1);
  EXPECT_EQ(formats.value().number_of_channels[0], 2);
  EXPECT_EQ(formats.value().sample_formats.size(), 1);
  EXPECT_EQ(formats.value().sample_formats[0], SampleFormat::PCM_SIGNED);
  EXPECT_EQ(formats.value().frame_formats.size(), 1);
  EXPECT_EQ(formats.value().frame_formats[0], FrameFormat::I2S);
  EXPECT_EQ(formats.value().frame_rates.size(), 1);
  EXPECT_EQ(formats.value().frame_rates[0], 48000);
  EXPECT_EQ(formats.value().bits_per_slot.size(), 1);
  EXPECT_EQ(formats.value().bits_per_slot[0], 32);
  EXPECT_EQ(formats.value().bits_per_sample.size(), 1);
  EXPECT_EQ(formats.value().bits_per_sample[0], 32);
  mock_i2c.VerifyAndClear();
}

TEST(Tas5782Test, GetInfo) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(
      fake_parent.get(), unused_i2c.GetProto(), std::move(unused_gpio0), std::move(unused_gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto info = client.GetInfo();
  EXPECT_EQ(info.value().unique_id.compare(""), 0);
  EXPECT_EQ(info.value().manufacturer.compare("Texas Instruments"), 0);
  EXPECT_EQ(info.value().product_name.compare("TAS5782m"), 0);
}

TEST(Tas5782Test, BridgedMode) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(
      fake_parent.get(), unused_i2c.GetProto(), std::move(unused_gpio0), std::move(unused_gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto bridgeable = client.IsBridgeable();
  ASSERT_FALSE(bridgeable.value());
}

TEST(Tas5782Test, GetGainFormat) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(
      fake_parent.get(), unused_i2c.GetProto(), std::move(unused_gpio0), std::move(unused_gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto format = client.GetGainFormat();
  EXPECT_EQ(format.value().min_gain, -103.0);
  EXPECT_EQ(format.value().max_gain, 24.0);
  EXPECT_EQ(format.value().gain_step, 0.5);
}

TEST(Tas5782Test, Init) {
  auto fake_parent = MockDevice::FakeRootParent();
  mock_i2c::MockI2c mock_i2c;
  mock_i2c
      .ExpectWriteStop({0x02, 0x10},
                       ZX_ERR_ADDRESS_UNREACHABLE)  // Enter standby, error will retry.
      .ExpectWriteStop({0x02, 0x10})                // Enter standby.
      .ExpectWriteStop({0x01, 0x11})                // Reset modules and registers.
      .ExpectWriteStop({0x0d, 0x10})                // The PLL reference clock is SCLK.
      .ExpectWriteStop({0x04, 0x01})                // PLL for MCLK setting.
      .ExpectWriteStop({0x28, 0x03})                // I2S, 32 bits.
      .ExpectWriteStop({0x2a, 0x22})   // Left DAC to Left ch, Right DAC to right channel.
      .ExpectWriteStop({0x02, 0x00});  // Exit standby.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::MockGpio mock_gpio0, mock_gpio1;
  ddk::GpioProtocolClient gpio0(mock_gpio0.GetProto());
  ddk::GpioProtocolClient gpio1(mock_gpio1.GetProto());
  mock_gpio0.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Reset, set to 0 and then to 1.
  mock_gpio1.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Set to mute and then to unmute.
  // Shutdown.
  mock_gpio0.ExpectWrite(ZX_OK, 0);  // Reset, set to 0 and then to 1.
  mock_gpio1.ExpectWrite(ZX_OK, 0);  // Set to mute.

  auto owned = SimpleCodecServer::Create<Tas5782Codec>(fake_parent.get(), std::move(i2c),
                                                       std::move(gpio0), std::move(gpio1));
  [[maybe_unused]] auto unused_raw_pointer = owned.release();  // codec release managed by the DDK.
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas5782Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  client.Reset();

  child_dev->ReleaseOp();
  mock_i2c.VerifyAndClear();
  mock_gpio0.VerifyAndClear();
  mock_gpio1.VerifyAndClear();
}
}  // namespace audio
