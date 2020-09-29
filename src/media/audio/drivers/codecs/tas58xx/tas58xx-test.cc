// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas58xx.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/sync/completion.h>

#include <string>
#include <thread>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

namespace audio {

struct Tas58xxCodec : public Tas58xx {
  explicit Tas58xxCodec(const ddk::I2cChannel& i2c) : Tas58xx(fake_ddk::kFakeParent, i2c) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

TEST(Tas58xxTest, GoodSetDai) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use = {0, 1};
    format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    format.frame_format = FRAME_FORMAT_I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    mock_i2c.ExpectWriteStop({0x33, 0x03});  // 32 bits.
    mock_i2c.ExpectWriteStop({0x34, 0x00});  // Keep data start sclk.
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_OK(client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use = {0, 1};
    format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    format.frame_format = FRAME_FORMAT_I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    mock_i2c.ExpectWriteStop({0x33, 0x00});  // 16 bits.
    mock_i2c.ExpectWriteStop({0x34, 0x00});  // Keep data start sclk.
    format.bits_per_sample = 16;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_OK(client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  {
    audio::DaiFormat format = {};
    format.number_of_channels = 4;
    format.channels_to_use = {2, 3};
    format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    format.frame_format = FRAME_FORMAT_TDM1;
    format.frame_rate = 48000;
    format.bits_per_slot = 16;
    format.bits_per_sample = 16;
    mock_i2c.ExpectWriteStop({0x33, 0x14});  // TDM/DSP, I2S_LRCLK_PULSE < 8 SCLK, 16 bits.
    mock_i2c.ExpectWriteStop({0x34, 0x20});  // Data start sclk at 32 bits.
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_OK(client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, BadSetDai) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // Blank format.
  {
    audio::DaiFormat format = {};
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  // Almost good format (wrong frame_format).
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use = {0, 1};
    format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    format.frame_format = FRAME_FORMAT_STEREO_LEFT;  // This must fail.
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  //   Almost good format (wrong channels).
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 1;
    format.channels_to_use = {0};
    format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    format.frame_format = FRAME_FORMAT_I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  // Almost good format (wrong rate).
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use = {0, 1};
    format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    format.frame_format = FRAME_FORMAT_I2S;
    format.frame_rate = 1234;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
      ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetDai) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    std::thread t([&]() {
      auto formats = client.GetDaiFormats();
      EXPECT_EQ(formats.value().size(), 1);
      EXPECT_EQ(formats.value()[0].number_of_channels.size(), 2);
      EXPECT_EQ(formats.value()[0].number_of_channels[0], 2);
      EXPECT_EQ(formats.value()[0].number_of_channels[1], 4);
      EXPECT_EQ(formats.value()[0].sample_formats.size(), 1);
      EXPECT_EQ(formats.value()[0].sample_formats[0], SAMPLE_FORMAT_PCM_SIGNED);
      EXPECT_EQ(formats.value()[0].frame_formats.size(), 2);
      EXPECT_EQ(formats.value()[0].frame_formats[0], FRAME_FORMAT_I2S);
      EXPECT_EQ(formats.value()[0].frame_formats[1], FRAME_FORMAT_TDM1);
      EXPECT_EQ(formats.value()[0].frame_rates.size(), 1);
      EXPECT_EQ(formats.value()[0].frame_rates[0], 48000);
      EXPECT_EQ(formats.value()[0].bits_per_slot.size(), 2);
      EXPECT_EQ(formats.value()[0].bits_per_slot[0], 16);
      EXPECT_EQ(formats.value()[0].bits_per_slot[1], 32);
      EXPECT_EQ(formats.value()[0].bits_per_sample.size(), 2);
      EXPECT_EQ(formats.value()[0].bits_per_sample[0], 16);
      EXPECT_EQ(formats.value()[0].bits_per_sample[1], 32);
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetInfo5805) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.
    std::thread t([&]() {
      auto info = client.GetInfo();
      EXPECT_EQ(info.value().unique_id.compare(""), 0);
      EXPECT_EQ(info.value().manufacturer.compare("Texas Instruments"), 0);
      EXPECT_EQ(info.value().product_name.compare("TAS5805m"), 0);
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetInfo5825) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.
    std::thread t([&]() {
      auto info = client.GetInfo();
      EXPECT_EQ(info.value().unique_id.compare(""), 0);
      EXPECT_EQ(info.value().manufacturer.compare("Texas Instruments"), 0);
      EXPECT_EQ(info.value().product_name.compare("TAS5825m"), 0);
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, CheckState) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    std::thread t([&]() {
      auto info = client.IsBridgeable();
      EXPECT_EQ(info.value(), false);

      auto format = client.GetGainFormat();
      EXPECT_EQ(format.value().min_gain_db, -103.0);
      EXPECT_EQ(format.value().max_gain_db, 24.0);
      EXPECT_EQ(format.value().gain_step_db, 0.5);

      auto state = client.GetPlugState();
      EXPECT_EQ(state.value().hardwired, true);
      EXPECT_EQ(state.value().plugged, true);
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, SetGain) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    mock_i2c
        .ExpectWriteStop({0x4c, 0x48})  // digital vol -12dB.
        .ExpectWrite({0x03})
        .ExpectReadStop({0x00})
        .ExpectWriteStop({0x03, 0x00});  // Muted = false.
    std::thread t([&]() {
      GainState gain({.gain_db = -12.f, .muted = false, .agc_enable = false});
      client.SetGainState(gain);
    });
    t.join();
  }

  {
    mock_i2c
        .ExpectWriteStop({0x4c, 0x60})  // digital vol -24dB.
        .ExpectWrite({0x03})
        .ExpectReadStop({0x00})
        .ExpectWriteStop({0x03, 0x08});  // Muted = true.
    std::thread t([&]() {
      GainState gain({.gain_db = -24.f, .muted = true, .agc_enable = false});
      client.SetGainState(gain);
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, Reset) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    mock_i2c
        .ExpectWriteStop({0x00, 0x00})  // Page 0.
        .ExpectWriteStop({0x7f, 0x00})  // book 0.
        .ExpectWriteStop({0x03, 0x02})  // HiZ, Enables DSP.
        .ExpectWriteStop({0x01, 0x11})  // Reset.
        .ExpectWriteStop({0x00, 0x00})  // Page 0.
        .ExpectWriteStop({0x7f, 0x00})  // book 0.
        .ExpectWriteStop({0x02, 0x01})  // Normal modulation, mono, no PBTL (Stereo BTL).
        .ExpectWriteStop({0x03, 0x03})  // Play,
        .ExpectWriteStop({0x00, 0x00})  // Page 0.
        .ExpectWriteStop({0x7f, 0x00})  // book 0.
        .ExpectWriteStop({0x78, 0x80})  // Clear analog fault.
        .ExpectWriteStop({0x4c, 0x6c})  // digital vol -30dB.
        .ExpectWrite({0x03})
        .ExpectReadStop({0x00})
        .ExpectWriteStop({0x03, 0x08});  // Muted = true.
    std::thread t([&]() { ASSERT_OK(client.Reset()); });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, Bridged) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  fake_ddk::Bind ddk;

  metadata::ti::TasConfig metadata = {};
  metadata.bridged = true;
  ddk.SetMetadata(&metadata, sizeof(metadata));

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    // Reset with PBTL mode on.
    mock_i2c
        .ExpectWriteStop({0x00, 0x00})  // Page 0.
        .ExpectWriteStop({0x7f, 0x00})  // book 0.
        .ExpectWriteStop({0x03, 0x02})  // HiZ, Enables DSP.
        .ExpectWriteStop({0x01, 0x11})  // Reset.
        .ExpectWriteStop({0x00, 0x00})  // Page 0.
        .ExpectWriteStop({0x7f, 0x00})  // book 0.
        .ExpectWriteStop({0x02, 0x05})  // Normal modulation, mono, PBTL (bridged mono).
        .ExpectWriteStop({0x03, 0x03})  // Play,
        .ExpectWriteStop({0x00, 0x00})  // Page 0.
        .ExpectWriteStop({0x7f, 0x00})  // book 0.
        .ExpectWriteStop({0x78, 0x80})  // Clear analog fault.
        .ExpectWriteStop({0x4c, 0x6c})  // digital vol -30dB.
        .ExpectWrite({0x03})
        .ExpectReadStop({0x00})
        .ExpectWriteStop({0x03, 0x08});  // Muted = true.
    std::thread t([&]() { ASSERT_OK(client.Reset()); });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, StopStart) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  fake_ddk::Bind ddk;

  metadata::ti::TasConfig metadata = {};
  metadata.bridged = true;
  ddk.SetMetadata(&metadata, sizeof(metadata));

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    // Reset with PBTL mode on.
    mock_i2c.ExpectWrite({0x03}).ExpectReadStop({0x00}).ExpectWriteStop(
        {0x03, 0x02});  // Stop, go to HiZ.
    mock_i2c.ExpectWrite({0x03}).ExpectReadStop({0x00}).ExpectWriteStop(
        {0x03, 0x03});  // Start, go back to play mode.
    std::thread t([&]() {
      ASSERT_OK(client.Stop());
      ASSERT_OK(client.Start());
    });
    t.join();
  }

  mock_i2c.VerifyAndClear();
}

}  // namespace audio
