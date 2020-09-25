// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5720.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/sync/completion.h>

#include <thread>

#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace {

audio::DaiFormat GetDefaultDaiFormat() {
  return {
      .number_of_channels = 2,
      .channels_to_use = std::vector<uint32_t>{0},  // Use one channel in this mono codec.
      .sample_format = SAMPLE_FORMAT_PCM_SIGNED,
      .frame_format = FRAME_FORMAT_STEREO_LEFT,
      .frame_rate = 24'000,
      .bits_per_channel = 32,
      .bits_per_sample = 16,
  };
}
}  // namespace

namespace audio {

class Tas5720Test : public zxtest::Test {
 public:
  void SetUp() override {
    // Reset by the TAS driver initialization.
    mock_i2c_.ExpectWrite({0x01})
        .ExpectReadStop({0xff})
        .ExpectWriteStop({0x01, 0xfe})  // Enter shutdown (part of reset).
        .ExpectWrite({0x01})
        .ExpectReadStop({0xfe})
        .ExpectWriteStop({0x01, 0xff})  // Exit shutdown (part of reset).
        .ExpectWrite({0x01})
        .ExpectReadStop({0xff})
        .ExpectWriteStop({0x01, 0xfe})  // Enter shutdown (part of stop).
        .ExpectWriteStop({0x02, 0x45})  // Digital control defaults. Left justified.
        .ExpectWriteStop({0x03, 0x10})  // Digital control defaults. Slot 0, muted.
        .ExpectWriteStop({0x06, 0x5d})  // Analog defaults.
        .ExpectWriteStop({0x10, 0xff})  // clippers disabled.
        .ExpectWriteStop({0x11, 0xfc})  // clippers disabled.
        .ExpectWrite({0x01})
        .ExpectReadStop({0xfe})
        .ExpectWriteStop({0x01, 0xff})  // exit shutdown (part of start).
        .ExpectWriteStop({0x06, 0x51})  // Default gain.
        .ExpectWriteStop({0x04, 0xa1})  // Default gain.
        .ExpectWrite({0x03})
        .ExpectReadStop({0x00})
        .ExpectWriteStop({0x03, 0x10});  // Muted.
  }
  mock_i2c::MockI2c mock_i2c_;
};

struct Tas5720Codec : public Tas5720 {
  explicit Tas5720Codec(ddk::I2cChannel i2c) : Tas5720(fake_ddk::kFakeParent, std::move(i2c)) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

TEST_F(Tas5720Test, CodecInitGood) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));
  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST(Tas5720Test, CodecInitBad) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));

  mock_i2c::MockI2c mock_i2c;
  // Bad reply to enter shutdown (part of reset).
  mock_i2c.ExpectWrite({0x01}).ExpectReadStop({0xff}, ZX_ERR_TIMED_OUT);
  // Bad reply to enter shutdown (part of shutdown becuase init failed).
  mock_i2c.ExpectWrite({0x01}).ExpectReadStop({0xff}, ZX_ERR_TIMED_OUT);

  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c.GetProto());
  ASSERT_NULL(codec);
  mock_i2c.VerifyAndClear();
}

TEST_F(Tas5720Test, CodecGetInfo) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));
  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  std::thread t([&]() {
    auto info = client.GetInfo();
    ASSERT_EQ(info->unique_id.compare(""), 0);
    ASSERT_EQ(info->manufacturer.compare("Texas Instruments"), 0);
    ASSERT_EQ(info->product_name.compare("TAS5720"), 0);
  });
  t.join();

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST_F(Tas5720Test, CodecReset) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));

  // Reset by the call to Reset.
  mock_i2c_.ExpectWrite({0x01})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x01, 0xfe})  // Enter shutdown (part of reset).
      .ExpectWrite({0x01})
      .ExpectReadStop({0xfe})
      .ExpectWriteStop({0x01, 0xff})  // Exit shutdown (part of reset).
      .ExpectWrite({0x01})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x01, 0xfe})  // Enter shutdown (part of stop).
      .ExpectWriteStop({0x02, 0x45})  // Digital control defaults. TODO set I2S.
      .ExpectWriteStop({0x03, 0x10})  // Digital control defaults. Slot 0, muted.
      .ExpectWriteStop({0x06, 0x5d})  // Analog defaults.
      .ExpectWriteStop({0x10, 0xff})  // clippers disabled.
      .ExpectWriteStop({0x11, 0xfc})  // clippers disabled.
      .ExpectWrite({0x01})
      .ExpectReadStop({0xfe})
      .ExpectWriteStop({0x01, 0xff})  // exit shutdown (part of start).
      .ExpectWriteStop({0x06, 0x51})  // Default gain.
      .ExpectWriteStop({0x04, 0xa1})  // Default gain.
      .ExpectWrite({0x03})
      .ExpectReadStop({0x00})
      .ExpectWriteStop({0x03, 0x10});  // Muted.

  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  std::thread t([&]() { ASSERT_OK(client.Reset()); });
  t.join();

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST_F(Tas5720Test, CodecBridgedMode) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));
  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  {
    std::thread t([&]() {
      auto bridgeable = client.IsBridgeable();
      ASSERT_FALSE(bridgeable.value());
    });
    t.join();
  }
  {
    std::thread t([&]() { client.SetBridgedMode(false); });
    t.join();
  }

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST_F(Tas5720Test, CodecDaiFormat) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));
  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // Check getting DAI formats.
  {
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_EQ(formats->size(), 1);
      ASSERT_EQ(formats.value()[0].number_of_channels.size(), 1);
      ASSERT_EQ(formats.value()[0].number_of_channels[0], 2);
      ASSERT_EQ(formats.value()[0].sample_formats.size(), 1);
      ASSERT_EQ(formats.value()[0].sample_formats[0], SAMPLE_FORMAT_PCM_SIGNED);
      ASSERT_EQ(formats.value()[0].frame_formats.size(), 2);
      ASSERT_EQ(formats.value()[0].frame_formats[0], FRAME_FORMAT_STEREO_LEFT);
      ASSERT_EQ(formats.value()[0].frame_formats[1], FRAME_FORMAT_I2S);
      ASSERT_EQ(formats.value()[0].frame_rates.size(), 2);
      ASSERT_EQ(formats.value()[0].frame_rates[0], 48000);
      ASSERT_EQ(formats.value()[0].frame_rates[1], 96000);
      ASSERT_EQ(formats.value()[0].bits_per_channel.size(), 1);
      ASSERT_EQ(formats.value()[0].bits_per_channel[0], 32);
      ASSERT_EQ(formats.value()[0].bits_per_sample.size(), 1);
      ASSERT_EQ(formats.value()[0].bits_per_sample[0], 16);
    });
    t.join();
  }

  // Check setting DAI formats.
  {
    mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0xff});
    mock_i2c_.ExpectWriteStop({0x03, 0xfc});  // Set slot to 0.
    mock_i2c_.ExpectWriteStop({0x02, 0x45});  // Set rate to 48kHz.
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_rate = 48'000;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_OK(client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  {
    mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0xff});
    mock_i2c_.ExpectWriteStop({0x03, 0xfc});  // Set slot to 0.
    mock_i2c_.ExpectWriteStop({0x02, 0x4d});  // Set rate to 96kHz.
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_rate = 96'000;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_OK(client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  {
    mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0xff});
    mock_i2c_.ExpectWriteStop({0x03, 0xfc});  // Set slot to 0.
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_rate = 192'000;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_FALSE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_NOT_OK(client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST_F(Tas5720Test, CodecGain) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));
  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  mock_i2c_
      .ExpectWriteStop({0x06, 0x51})  // Analog 19.2dBV.
      .ExpectWriteStop({0x04, 0x9d})  // Digital -32dB.
      .ExpectWrite({0x03})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x03, 0xef});  // Not muted.
  client.SetGainState({
      .gain_db = -32.f,
      .muted = false,
      .agc_enable = false,
  });

  // Lower than min gain.
  mock_i2c_
      .ExpectWriteStop({0x06, 0x51})  // Analog 19.2dBV (min)
      .ExpectWriteStop({0x04, 0x00})  // Digital -110.6dB.
      .ExpectWrite({0x03})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x03, 0xef});  // Not muted.
  client.SetGainState({
      .gain_db = -999.f,
      .muted = false,
      .agc_enable = false,
  });

  // Higher than max gain.
  mock_i2c_
      .ExpectWriteStop({0x06, 0x5d})  // Analog 23.5dBV (max).
      .ExpectWriteStop({0x04, 0xff})  // Digital +24dB.
      .ExpectWrite({0x03})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x03, 0xef});  // Not muted.
  client.SetGainState({
      .gain_db = 111.f,
      .muted = false,
      .agc_enable = false,
  });

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST_F(Tas5720Test, CodecPlugState) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 0;
  tester.SetMetadata(&instance_count, sizeof(instance_count));
  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c_.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  std::thread t([&]() {
    auto state = client.GetPlugState();
    ASSERT_TRUE(state->hardwired);
    ASSERT_TRUE(state->plugged);
  });
  t.join();

  // Shutdown.
  mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c_.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

TEST(Tas5720Test, InstanceCount) {
  fake_ddk::Bind tester;
  uint32_t instance_count = 2;
  tester.SetMetadata(&instance_count, sizeof(instance_count));

  mock_i2c::MockI2c mock_i2c;

  // Reset by the TAS driver initialization setting slot to 2.
  mock_i2c.ExpectWrite({0x01})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x01, 0xfe})  // Enter shutdown (part of reset).
      .ExpectWrite({0x01})
      .ExpectReadStop({0xfe})
      .ExpectWriteStop({0x01, 0xff})  // Exit shutdown (part of reset).
      .ExpectWrite({0x01})
      .ExpectReadStop({0xff})
      .ExpectWriteStop({0x01, 0xfe})  // Enter shutdown (part of stop).
      .ExpectWriteStop({0x02, 0x45})  // Digital control defaults. TODO set I2S.
      .ExpectWriteStop({0x03, 0x10})  // Digital control defaults. Slot 2, muted.
      .ExpectWriteStop({0x06, 0x5d})  // Analog defaults.
      .ExpectWriteStop({0x10, 0xff})  // clippers disabled.
      .ExpectWriteStop({0x11, 0xfc})  // clippers disabled.
      .ExpectWrite({0x01})
      .ExpectReadStop({0xfe})
      .ExpectWriteStop({0x01, 0xff})  // exit shutdown (part of start).
      .ExpectWriteStop({0x06, 0x51})  // Default gain.
      .ExpectWriteStop({0x04, 0xa1})  // Default gain.
      .ExpectWrite({0x03})
      .ExpectReadStop({0x00})
      .ExpectWriteStop({0x03, 0x10});  // Muted.

  auto codec = SimpleCodecServer::Create<Tas5720Codec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);

  // Shutdown.
  mock_i2c.ExpectWrite({0x01}).ExpectReadStop({0xff}).ExpectWriteStop({0x01, 0xfe});

  codec->DdkAsyncRemove();
  codec->DdkRelease();
  codec.release();  // codec is managed by the DDK.

  mock_i2c.VerifyAndClear();
  ASSERT_TRUE(tester.Ok());
}

}  // namespace audio
