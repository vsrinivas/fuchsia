// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/media/audio/drivers/codecs/da7219/da7219.h"

namespace audio {

using inspect::InspectTestHelper;

struct Da7219Codec : public Da7219 {
  explicit Da7219Codec(zx_device_t* parent, fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c)
      : Da7219(parent, std::move(i2c)) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

class Da7219Test : public InspectTestHelper, public zxtest::Test {
 public:
  Da7219Test() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    // IDs check.
    mock_i2c_.ExpectWrite({0x81}).ExpectReadStop({0x23}, ZX_OK);
    mock_i2c_.ExpectWrite({0x82}).ExpectReadStop({0x93}, ZX_OK);
    mock_i2c_.ExpectWrite({0x83}).ExpectReadStop({0x02}, ZX_OK);

    fake_root_ = MockDevice::FakeRootParent();
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    EXPECT_TRUE(endpoints.is_ok());

    EXPECT_OK(loop_.StartThread());
    fidl::BindServer<mock_i2c::MockI2c>(loop_.dispatcher(), std::move(endpoints->server),
                                        &mock_i2c_);
    ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Da7219Codec>(fake_root_.get(),
                                                                std::move(endpoints->client)));
  }

  void TearDown() override { mock_i2c_.VerifyAndClear(); }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  mock_i2c::MockI2c mock_i2c_;
  async::Loop loop_;
};

TEST_F(Da7219Test, GetInfo) {
  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto info = client.GetInfo();
  EXPECT_EQ(info.value().unique_id.compare(""), 0);
  EXPECT_EQ(info.value().manufacturer.compare("Dialog"), 0);
  EXPECT_EQ(info.value().product_name.compare("DA7219"), 0);
}

TEST_F(Da7219Test, Reset) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);  // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);  // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);  // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x88}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6c, 0x88}, ZX_OK);  // HP Routing.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  [[maybe_unused]] auto unused = client.Reset();
}

TEST_F(Da7219Test, GoodSetDai) {
  // Set DAI.
  mock_i2c_.ExpectWriteStop({0x2d, 0x00}, ZX_OK);  // TDM mode disabled.
  mock_i2c_.ExpectWriteStop({0x2c, 0xa8}, ZX_OK);  // 24 bits per sample.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 3;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 24;
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    auto codec_format_info = client.SetDaiFormat(std::move(format));
    // 5ms turn on delay expected.
    ASSERT_OK(codec_format_info.status_value());
    EXPECT_FALSE(codec_format_info->has_turn_off_delay());
    EXPECT_FALSE(codec_format_info->has_turn_on_delay());
  }
}

}  // namespace audio
