// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace audio {

struct Max98373Codec : public Max98373 {
  explicit Max98373Codec(ddk::I2cChannel i2c, ddk::GpioProtocolClient codec_reset,
                         zx_device_t* parent)
      : Max98373(parent, std::move(i2c), std::move(codec_reset)) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

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
        .ExpectWriteStop({0x20, 0x3d, 0x28})   // Set digital gain to -20dB.
        .ExpectWriteStop({0x20, 0x3e, 0x05})   // Set analog gain to +13dB.
        .ExpectWriteStop({0x20, 0x2b, 0x01});  // Data in enable.

    loop_.StartThread();

    fake_root_ = MockDevice::FakeRootParent();
    mock_gpio_.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Reset, set to 0 and then to 1.
    ddk::GpioProtocolClient gpio(mock_gpio_.GetProto());
    ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Max98373Codec>(GetI2cClient(), std::move(gpio),
                                                                  fake_root_.get()));
    auto* child_dev = fake_root_->GetLatestChild();
    auto codec = child_dev->GetDeviceContext<Max98373Codec>();
    auto codec_proto = codec->GetProto();
    client_.SetProtocol(&codec_proto);
  }

  void TearDown() override {
    auto* child_dev = fake_root_->GetLatestChild();
    child_dev->UnbindOp();
    mock_i2c_.VerifyAndClear();
    mock_gpio_.VerifyAndClear();
  }

  fidl::ClientEnd<fuchsia_hardware_i2c::Device> GetI2cClient() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    if (endpoints.is_error()) {
      return {};
    }

    fidl::BindServer<mock_i2c::MockI2c>(loop_.dispatcher(), std::move(endpoints->server),
                                        &mock_i2c_);
    return std::move(endpoints->client);
  }

 protected:
  mock_i2c::MockI2c mock_i2c_;
  ddk::MockGpio mock_gpio_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  SimpleCodecClient client_;
  std::shared_ptr<MockDevice> fake_root_;
};

TEST_F(Max98373Test, GetInfo) {
  auto info = client_.GetInfo();
  EXPECT_EQ(info.value().unique_id.compare(""), 0);
  EXPECT_EQ(info.value().manufacturer.compare("Maxim"), 0);
  EXPECT_EQ(info.value().product_name.compare("MAX98373"), 0);
}

TEST_F(Max98373Test, GetDaiFormats) {
  auto formats = client_.GetDaiFormats();
  EXPECT_EQ(formats.value().number_of_channels.size(), 4);
  EXPECT_EQ(formats.value().number_of_channels[0], 2);
  EXPECT_EQ(formats.value().number_of_channels[1], 4);
  EXPECT_EQ(formats.value().number_of_channels[2], 8);
  EXPECT_EQ(formats.value().number_of_channels[3], 16);
  EXPECT_EQ(formats.value().sample_formats.size(), 1);
  EXPECT_EQ(formats.value().sample_formats[0], SampleFormat::PCM_SIGNED);
  EXPECT_EQ(formats.value().frame_formats.size(), 3);
  EXPECT_EQ(formats.value().frame_formats[0], FrameFormat::TDM1);
  EXPECT_EQ(formats.value().frame_formats[1], FrameFormat::I2S);
  EXPECT_EQ(formats.value().frame_formats[2], FrameFormat::STEREO_LEFT);
  EXPECT_EQ(formats.value().frame_rates.size(), 8);
  EXPECT_EQ(formats.value().frame_rates[0], 16'000);
  EXPECT_EQ(formats.value().frame_rates[1], 22'050);
  EXPECT_EQ(formats.value().frame_rates[2], 24'000);
  EXPECT_EQ(formats.value().frame_rates[3], 32'000);
  EXPECT_EQ(formats.value().frame_rates[4], 44'100);
  EXPECT_EQ(formats.value().frame_rates[5], 48'000);
  EXPECT_EQ(formats.value().frame_rates[6], 88'200);
  EXPECT_EQ(formats.value().frame_rates[7], 96'000);
  EXPECT_EQ(formats.value().bits_per_slot.size(), 3);
  EXPECT_EQ(formats.value().bits_per_slot[0], 16);
  EXPECT_EQ(formats.value().bits_per_slot[1], 24);
  EXPECT_EQ(formats.value().bits_per_slot[2], 32);
  EXPECT_EQ(formats.value().bits_per_sample.size(), 3);
  EXPECT_EQ(formats.value().bits_per_sample[0], 16);
  EXPECT_EQ(formats.value().bits_per_sample[1], 24);
  EXPECT_EQ(formats.value().bits_per_sample[2], 32);
}

TEST_F(Max98373Test, Reset) {
  mock_i2c_
      .ExpectWriteStop({0x20, 0x00, 0x01}, ZX_ERR_INTERNAL)  // Reset, error will retry.
      .ExpectWriteStop({0x20, 0x00, 0x01}, ZX_ERR_INTERNAL)  // Reset, error will retry.
      .ExpectWriteStop({0x20, 0x00, 0x01})                   // Reset.
      .ExpectWrite({0x21, 0xff})
      .ExpectReadStop({0x43})                // Get revision id.
      .ExpectWriteStop({0x20, 0xff, 0x01})   // Global enable.
      .ExpectWriteStop({0x20, 0x43, 0x01})   // Speaker enable.
      .ExpectWriteStop({0x20, 0x3d, 0x28})   // Set digital gain to -20dB.
      .ExpectWriteStop({0x20, 0x3e, 0x05})   // Set analog gain to +13dB.
      .ExpectWriteStop({0x20, 0x2b, 0x01});  // Data in enable.

  ASSERT_OK(client_.Reset());
}

TEST_F(Max98373Test, SetDaiFormat) {
  // Good.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 2;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::I2S;
    format.frame_rate = 16'000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 16;
    mock_i2c_
        .ExpectWriteStop({0x20, 0x29, 0x01})   // Slot 1.
        .ExpectWriteStop({0x20, 0x26, 0x04})   // 64 ratio.
        .ExpectWriteStop({0x20, 0x24, 0x40})   // I2S 16 bits.
        .ExpectWriteStop({0x20, 0x27, 0x03});  // 16KHz
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client_.SetDaiFormat(std::move(format)));
  }
  // Good.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 2;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::STEREO_LEFT;
    format.frame_rate = 88'200;
    format.bits_per_slot = 32;
    format.bits_per_sample = 24;
    mock_i2c_
        .ExpectWriteStop({0x20, 0x29, 0x01})   // Slot 1.
        .ExpectWriteStop({0x20, 0x26, 0x04})   // 64 ratio.
        .ExpectWriteStop({0x20, 0x24, 0x88})   // Left justification 24 bits.
        .ExpectWriteStop({0x20, 0x27, 0x09});  // 88.1KHz
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client_.SetDaiFormat(std::move(format)));
  }
  // Bad.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 2;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::STEREO_LEFT;
    format.frame_rate = 88'200;
    format.bits_per_slot = 16;
    format.bits_per_sample = 32;  // 32 bits into 16 is not valid.
    auto formats = client_.GetDaiFormats();
    ASSERT_FALSE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_NOT_OK(client_.SetDaiFormat(std::move(format)));
  }
}

TEST_F(Max98373Test, SetDaiFormatTdmSlot) {
  // Slot 0 ok.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 8;
    format.channels_to_use_bitmask = 1;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 32'000;
    format.bits_per_slot = 16;
    format.bits_per_sample = 16;
    mock_i2c_
        .ExpectWriteStop({0x20, 0x29, 0x00})   // Slot 0.
        .ExpectWriteStop({0x20, 0x26, 0x06})   // 128 ratio for 8 x 16 bits.
        .ExpectWriteStop({0x20, 0x24, 0x58})   // TDM 16 bits.
        .ExpectWriteStop({0x20, 0x27, 0x06});  // 32KHz
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client_.SetDaiFormat(std::move(format)));
  }

  // Slot 1 ok.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 8;
    format.channels_to_use_bitmask = 2;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 44'100;
    format.bits_per_slot = 32;
    format.bits_per_sample = 24;
    mock_i2c_
        .ExpectWriteStop({0x20, 0x29, 0x01})   // Slot 1.
        .ExpectWriteStop({0x20, 0x26, 0x08})   // 256 ratio for 8 x 32 bits.
        .ExpectWriteStop({0x20, 0x24, 0x98})   // TDM 24 bits.
        .ExpectWriteStop({0x20, 0x27, 0x07});  // 44.1KHz
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client_.SetDaiFormat(std::move(format)));
  }

  // Slot 2 ok.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 4;
    format.channels_to_use_bitmask = 4;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 44'100;
    format.bits_per_slot = 32;
    format.bits_per_sample = 24;
    mock_i2c_
        .ExpectWriteStop({0x20, 0x29, 0x02})   // Slot 2.
        .ExpectWriteStop({0x20, 0x26, 0x06})   // 128 ratio for 4 x 32 bits.
        .ExpectWriteStop({0x20, 0x24, 0x98})   // TDM 24 bits.
        .ExpectWriteStop({0x20, 0x27, 0x07});  // 44.1KHz
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client_.SetDaiFormat(std::move(format)));
  }

  // Slot 15 ok.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 16;
    format.channels_to_use_bitmask = 0x8000;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 24'000;
    format.bits_per_slot = 16;
    format.bits_per_sample = 16;
    mock_i2c_
        .ExpectWriteStop({0x20, 0x29, 0x0f})   // Slot 15.
        .ExpectWriteStop({0x20, 0x26, 0x08})   // 256 ratio for 16 slots x 16 bits.
        .ExpectWriteStop({0x20, 0x24, 0x58})   // TDM 16 bits.
        .ExpectWriteStop({0x20, 0x27, 0x05});  // 24KHz
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client_.SetDaiFormat(std::move(format)));
  }

  // Multiple slots not supported.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 8;
    format.channels_to_use_bitmask = 0x8080;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 48'000;
    format.bits_per_slot = 16;
    format.bits_per_sample = 16;
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    zx::result<CodecFormatInfo> format_info = client_.SetDaiFormat(std::move(format));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, format_info.status_value());
  }

  // Slot 16 not supported.
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 8;
    format.channels_to_use_bitmask = 0x1'0000;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 48'000;
    format.bits_per_slot = 16;
    format.bits_per_sample = 16;
    auto formats = client_.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    zx::result<CodecFormatInfo> format_info = client_.SetDaiFormat(std::move(format));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, format_info.status_value());
  }
}

TEST_F(Max98373Test, SetGainGood) {
  mock_i2c_.ExpectWriteStop({0x20, 0x3d, 0x40});  // -32dB.

  GainState gain({.gain = -32.f, .muted = false, .agc_enabled = false});
  client_.SetGainState(gain);

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  auto unused = client_.GetInfo();
  static_cast<void>(unused);
}

TEST_F(Max98373Test, SetGainOurOfRangeLow) {
  mock_i2c_.ExpectWriteStop({0x20, 0x3d, 0x7f});  // -63.5dB.

  GainState gain({.gain = -999.f, .muted = false, .agc_enabled = false});
  client_.SetGainState(gain);

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  auto unused = client_.GetInfo();
  static_cast<void>(unused);
}

TEST_F(Max98373Test, SetGainOurOfRangeHigh) {
  mock_i2c_.ExpectWriteStop({0x20, 0x3d, 0x00});  // 0dB.

  GainState gain({.gain = 999.f, .muted = false, .agc_enabled = false});
  client_.SetGainState(gain);

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  auto unused = client_.GetInfo();
  static_cast<void>(unused);
}

}  // namespace audio
