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

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

namespace audio {

struct Tas58xxCodec : public Tas58xx {
  explicit Tas58xxCodec(const ddk::I2cChannel& i2c) : Tas58xx(fake_ddk::kFakeParent, i2c) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

TEST(Tas58xxTest, GoodSetDai) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00}, ZX_ERR_INTERNAL);  // Error will retry.
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00}, ZX_ERR_INTERNAL);  // Error will retry.
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00}, ZX_OK);  // Check DIE ID, no error now.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
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
    format.bits_per_sample = 32;
    mock_i2c.ExpectWriteStop({0x33, 0x03});  // 32 bits.
    mock_i2c.ExpectWriteStop({0x34, 0x00});  // Keep data start sclk.
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client.SetDaiFormat(std::move(format)));
  }

  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 3;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    mock_i2c.ExpectWriteStop({0x33, 0x00});  // 16 bits.
    mock_i2c.ExpectWriteStop({0x34, 0x00});  // Keep data start sclk.
    format.bits_per_sample = 16;
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client.SetDaiFormat(std::move(format)));
  }

  {
    audio::DaiFormat format = {};
    format.number_of_channels = 4;
    format.channels_to_use_bitmask = 0xc;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::TDM1;
    format.frame_rate = 48000;
    format.bits_per_slot = 16;
    format.bits_per_sample = 16;
    mock_i2c.ExpectWriteStop({0x33, 0x14});  // TDM/DSP, I2S_LRCLK_PULSE < 8 SCLK, 16 bits.
    mock_i2c.ExpectWriteStop({0x34, 0x20});  // Data start sclk at 32 bits.
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_OK(client.SetDaiFormat(std::move(format)));
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, BadSetDai) {
  fake_ddk::Bind tester;
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
    auto formats = client.GetDaiFormats();
    EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, client.SetDaiFormat(std::move(format)));
  }

  // Almost good format (wrong frame_format).
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 3;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::STEREO_LEFT;  // This must fail.
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    auto formats = client.GetDaiFormats();
    EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
  }

  //   Almost good format (wrong channels).
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 1;
    format.channels_to_use_bitmask = 1;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    auto formats = client.GetDaiFormats();
    EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
  }

  // Almost good format (wrong rate).
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 3;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::I2S;
    format.frame_rate = 1234;
    format.bits_per_slot = 32;
    format.bits_per_sample = 32;
    auto formats = client.GetDaiFormats();
    EXPECT_FALSE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, client.SetDaiFormat(std::move(format)));
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetDai) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    auto formats = client.GetDaiFormats();
    EXPECT_EQ(formats.value().size(), 1);
    EXPECT_EQ(formats.value()[0].number_of_channels.size(), 2);
    EXPECT_EQ(formats.value()[0].number_of_channels[0], 2);
    EXPECT_EQ(formats.value()[0].number_of_channels[1], 4);
    EXPECT_EQ(formats.value()[0].sample_formats.size(), 1);
    EXPECT_EQ(formats.value()[0].sample_formats[0], SampleFormat::PCM_SIGNED);
    EXPECT_EQ(formats.value()[0].frame_formats.size(), 2);
    EXPECT_EQ(formats.value()[0].frame_formats[0], FrameFormat::I2S);
    EXPECT_EQ(formats.value()[0].frame_formats[1], FrameFormat::TDM1);
    EXPECT_EQ(formats.value()[0].frame_rates.size(), 1);
    EXPECT_EQ(formats.value()[0].frame_rates[0], 48000);
    EXPECT_EQ(formats.value()[0].bits_per_slot.size(), 2);
    EXPECT_EQ(formats.value()[0].bits_per_slot[0], 16);
    EXPECT_EQ(formats.value()[0].bits_per_slot[1], 32);
    EXPECT_EQ(formats.value()[0].bits_per_sample.size(), 2);
    EXPECT_EQ(formats.value()[0].bits_per_sample[0], 16);
    EXPECT_EQ(formats.value()[0].bits_per_sample[1], 32);
  }

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetInfo5805) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.
    auto info = client.GetInfo();
    EXPECT_EQ(info.value().unique_id.compare(""), 0);
    EXPECT_EQ(info.value().manufacturer.compare("Texas Instruments"), 0);
    EXPECT_EQ(info.value().product_name.compare("TAS5805m"), 0);
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetInfo5825) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.
    auto info = client.GetInfo();
    EXPECT_EQ(info.value().unique_id.compare(""), 0);
    EXPECT_EQ(info.value().manufacturer.compare("Texas Instruments"), 0);
    EXPECT_EQ(info.value().product_name.compare("TAS5825m"), 0);
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, CheckState) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    auto info = client.IsBridgeable();
    EXPECT_EQ(info.value(), false);

    auto format = client.GetGainFormat();
    EXPECT_EQ(format.value().min_gain, -103.0);
    EXPECT_EQ(format.value().max_gain, 24.0);
    EXPECT_EQ(format.value().gain_step, 0.5);

    auto state = client.GetPlugState();
    EXPECT_EQ(state.value().hardwired, true);
    EXPECT_EQ(state.value().plugged, true);
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, SetGain) {
  fake_ddk::Bind tester;
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
    GainState gain({.gain = -12.f, .muted = false, .agc_enable = false});
    client.SetGainState(gain);
  }

  {
    mock_i2c
        .ExpectWriteStop({0x4c, 0x60})  // digital vol -24dB.
        .ExpectWrite({0x03})
        .ExpectReadStop({0x00})
        .ExpectWriteStop({0x03, 0x08});  // Muted = true.
    GainState gain({.gain = -24.f, .muted = true, .agc_enable = false});
    client.SetGainState(gain);
  }

  // Make a 2-wal call to make sure the server (we know single threaded) completed previous calls.
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});  // Check DIE ID.
  auto unused = client.GetInfo();
  static_cast<void>(unused);

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, Reset) {
  fake_ddk::Bind tester;
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
    ASSERT_OK(client.Reset());
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, Bridged) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  metadata::ti::TasConfig metadata = {};
  metadata.bridged = true;
  tester.SetMetadata(&metadata, sizeof(metadata));

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
    ASSERT_OK(client.Reset());
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, StopStart) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  metadata::ti::TasConfig metadata = {};
  metadata.bridged = true;
  tester.SetMetadata(&metadata, sizeof(metadata));

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
    ASSERT_OK(client.Stop());
    ASSERT_OK(client.Start());
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, ExternalConfig) {
  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});  // Check DIE ID.

  metadata::ti::TasConfig metadata = {};
  metadata.bridged = true;
  metadata.number_of_writes1 = 2;
  metadata.init_sequence1[0].address = 0x12;
  metadata.init_sequence1[0].value = 0x34;
  metadata.init_sequence1[1].address = 0x56;
  metadata.init_sequence1[1].value = 0x78;
  metadata.number_of_writes2 = 3;
  metadata.init_sequence2[0].address = 0x11;
  metadata.init_sequence2[0].value = 0x22;
  metadata.init_sequence2[1].address = 0x33;
  metadata.init_sequence2[1].value = 0x44;
  metadata.init_sequence2[2].address = 0x55;
  metadata.init_sequence2[2].value = 0x66;
  tester.SetMetadata(&metadata, sizeof(metadata));

  auto codec = SimpleCodecServer::Create<Tas58xxCodec>(mock_i2c.GetProto());
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  {
    // Reset with PBTL mode on.
    mock_i2c
        .ExpectWriteStop({0x12, 0x34})  // External config.
        .ExpectWriteStop({0x56, 0x78})  // External config.
        .ExpectWriteStop({0x11, 0x22})  // External config.
        .ExpectWriteStop({0x33, 0x44})  // External config.
        .ExpectWriteStop({0x55, 0x66})  // External config.
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
    ASSERT_OK(client.Reset());
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
  mock_i2c.VerifyAndClear();
}

}  // namespace audio
