// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

namespace {
static constexpr float kTestGain = 2.f;
static const char* kTestId = "test id";
static const char* kTestManufacturer = "test man";
static const char* kTestProduct = "test prod";
}  // namespace
namespace audio {

// Server tests.
struct TestCodec : public SimpleCodecServer {
  explicit TestCodec() : SimpleCodecServer(nullptr), proto_({&codec_protocol_ops_, this}) {}
  const codec_protocol_t* GetProto() { return &proto_; }

  zx_status_t Shutdown() override { return ZX_OK; }
  zx::status<DriverIds> Initialize() override {
    return zx::ok(DriverIds{.vendor_id = 0, .device_id = 0});
  }
  zx_status_t Reset() override { return ZX_OK; }
  Info GetInfo() override {
    return {.unique_id = kTestId, .manufacturer = kTestManufacturer, .product_name = kTestProduct};
  }
  zx_status_t Stop() override { return ZX_OK; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return bridged_; }
  void SetBridgedMode(bool enable_bridged_mode) override { bridged_ = enable_bridged_mode; }
  std::vector<DaiSupportedFormats> GetDaiFormats() override {
    std::vector<DaiSupportedFormats> formats;
    {
      DaiSupportedFormats format;
      format.number_of_channels.push_back(2);
      format.sample_formats.push_back(SAMPLE_FORMAT_PCM_SIGNED);
      format.frame_formats.push_back(FRAME_FORMAT_I2S);
      format.frame_rates.push_back(48'000);
      format.frame_rates.push_back(96'000);
      format.bits_per_slot.push_back(16);
      format.bits_per_slot.push_back(32);
      format.bits_per_sample.push_back(16);
      format.bits_per_sample.push_back(24);
      format.bits_per_sample.push_back(32);
      formats.push_back(std::move(format));
    }
    {
      DaiSupportedFormats format;
      format.number_of_channels.push_back(2);
      format.sample_formats.push_back(SAMPLE_FORMAT_PCM_SIGNED);
      format.frame_formats.push_back(FRAME_FORMAT_STEREO_LEFT);
      format.frame_rates.push_back(24'000);
      format.bits_per_slot.push_back(32);
      format.bits_per_sample.push_back(32);
      formats.push_back(std::move(format));
    }
    return formats;
  }
  zx_status_t SetDaiFormat(const DaiFormat& format) override {
    return (format.bits_per_sample == 16 || format.bits_per_sample == 24 ||
            format.bits_per_sample == 32)
               ? ZX_OK
               : ZX_ERR_NOT_SUPPORTED;
  }
  GainFormat GetGainFormat() override {
    return {
        .min_gain_db = -10.f,
        .max_gain_db = 10.f,
        .gain_step_db = 1.f,
        .can_mute = true,
        .can_agc = false,
    };
  }
  GainState GetGainState() override { return {.gain_db = 5.f, .muted = true, .agc_enable = false}; }
  void SetGainState(GainState state) override { ZX_ASSERT(state.gain_db == kTestGain); }
  PlugState GetPlugState() override { return {.hardwired = false, .plugged = true}; }

  bool bridged_ = false;
  codec_protocol_t proto_ = {};
};

TEST(SimpleCodecTest, ServerProtocolPassThroughMainControls) {
  auto device = SimpleCodecServer::Create<TestCodec>();

  device->CodecReset([](void* ctx, zx_status_t status) { EXPECT_OK(status); }, nullptr);
  device->CodecStop([](void* ctx, zx_status_t status) { EXPECT_OK(status); }, nullptr);
  device->CodecStart([](void* ctx, zx_status_t status) { EXPECT_OK(status); }, nullptr);
  device->CodecGetInfo(
      [](void* ctx, const info_t* info) {
        EXPECT_EQ(strcmp(info->unique_id, kTestId), 0);
        EXPECT_EQ(strcmp(info->manufacturer, kTestManufacturer), 0);
        EXPECT_EQ(strcmp(info->product_name, kTestProduct), 0);
      },
      nullptr);
}

TEST(SimpleCodecTest, ServerProtocolPassThroughBridgedMode) {
  auto device = SimpleCodecServer::Create<TestCodec>();

  device->CodecIsBridgeable([](void* ctx, bool b) { EXPECT_FALSE(b); }, nullptr);
  device->CodecSetBridgedMode(
      false, [](void* ctx) {}, nullptr);
  device->CodecIsBridgeable([](void* ctx, bool b) { EXPECT_FALSE(b); }, nullptr);
  device->CodecSetBridgedMode(
      true, [](void* ctx) {}, nullptr);
  device->CodecIsBridgeable([](void* ctx, bool b) { EXPECT_TRUE(b); }, nullptr);
}

TEST(SimpleCodecTest, ServerProtocolPassThrougDaiFormat) {
  auto device = SimpleCodecServer::Create<TestCodec>();

  device->CodecGetDaiFormats(
      [](void* ctx, zx_status_t status, const dai_supported_formats_t* formats_list,
         size_t formats_count) {
        EXPECT_OK(status);
        EXPECT_EQ(formats_count, 2);
        EXPECT_EQ(formats_list[0].frame_rates_count, 2);
        EXPECT_EQ(formats_list[0].frame_rates_list[0], 48'000);
        EXPECT_EQ(formats_list[0].frame_rates_list[1], 96'000);
        EXPECT_EQ(formats_list[1].frame_rates_count, 1);
        EXPECT_EQ(formats_list[1].frame_rates_list[0], 24'000);
      },
      nullptr);
  dai_format_t format = {};
  format.bits_per_sample = 32;
  device->CodecSetDaiFormat(
      &format, [](void* ctx, zx_status_t status) { EXPECT_OK(status); }, nullptr);
  format.bits_per_sample = 8;
  device->CodecSetDaiFormat(
      &format, [](void* ctx, zx_status_t status) { EXPECT_NOT_OK(status); }, nullptr);
}

TEST(SimpleCodecTest, ServerProtocolPassThrougGainPlugStates) {
  auto device = SimpleCodecServer::Create<TestCodec>();

  device->CodecGetGainFormat(
      [](void* ctx, const gain_format_t* format) {
        EXPECT_EQ(format->type, GAIN_TYPE_DECIBELS);
        EXPECT_EQ(format->min_gain, -10.f);
        EXPECT_EQ(format->max_gain, 10.f);
        EXPECT_EQ(format->gain_step, 1.f);
        EXPECT_EQ(format->can_mute, true);
        EXPECT_EQ(format->can_agc, false);
      },
      nullptr);
  device->CodecGetGainState(
      [](void* ctx, const gain_state_t* state) {
        EXPECT_EQ(state->gain, 5.f);
        EXPECT_EQ(state->muted, true);
        EXPECT_EQ(state->agc_enable, false);
      },
      nullptr);
  gain_state_t gain_state = {.gain = kTestGain, .muted = false, .agc_enable = false};
  device->CodecSetGainState(
      &gain_state, [](void* ctx) {}, nullptr);
  device->CodecGetPlugState(
      [](void* ctx, const plug_state_t* state) {
        EXPECT_EQ(state->hardwired, false);
        EXPECT_EQ(state->plugged, true);
      },
      nullptr);
}

// Client tests.
struct TestCodecClient : public SimpleCodecClient {
  void SetProto(const codec_protocol_t* proto) { proto_client_ = proto; }
};

TEST(SimpleCodecTest, ClientProtocolPassThroughMainControls) {
  auto codec = std::make_unique<TestCodec>();
  TestCodecClient client;
  client.SetProto(codec->GetProto());

  ASSERT_OK(client.Reset());
  ASSERT_OK(client.Stop());
  ASSERT_OK(client.Start());

  auto info = client.GetInfo();
  ASSERT_TRUE(info.is_ok());
  ASSERT_EQ(info->unique_id.compare(kTestId), 0);
  ASSERT_EQ(info->manufacturer.compare(kTestManufacturer), 0);
  ASSERT_EQ(info->product_name.compare(kTestProduct), 0);
}

TEST(SimpleCodecTest, ClientProtocolPassThroughBridgedMode) {
  auto codec = std::make_unique<TestCodec>();
  TestCodecClient client;
  client.SetProto(codec->GetProto());

  auto bridgable = client.IsBridgeable();
  ASSERT_TRUE(bridgable.is_ok());
  ASSERT_FALSE(bridgable.value());

  ASSERT_OK(client.SetBridgedMode(false));
}

TEST(SimpleCodecTest, ClientProtocolPassThroughDaiFormat) {
  auto codec = std::make_unique<TestCodec>();
  TestCodecClient client;
  client.SetProto(codec->GetProto());

  auto formats = client.GetDaiFormats();
  ASSERT_TRUE(formats.is_ok());
  ASSERT_EQ(formats->size(), 2);
  ASSERT_EQ(formats.value()[0].bits_per_sample.size(), 3);
  ASSERT_EQ(formats.value()[0].bits_per_sample[0], 16);
  ASSERT_EQ(formats.value()[0].bits_per_sample[1], 24);
  ASSERT_EQ(formats.value()[0].bits_per_sample[2], 32);
  ASSERT_EQ(formats.value()[1].bits_per_sample.size(), 1);
  ASSERT_EQ(formats.value()[1].bits_per_sample[0], 32);

  DaiFormat format = {
      .number_of_channels = 2,
      .sample_format = SAMPLE_FORMAT_PCM_SIGNED,
      .frame_format = FRAME_FORMAT_I2S,
      .frame_rate = 48'000,
      .bits_per_slot = 32,
      .bits_per_sample = 0,  // Bad.
  };
  ASSERT_FALSE(IsDaiFormatSupported(format, formats.value()));
  ASSERT_NOT_OK(client.SetDaiFormat(std::move(format)));
  format.bits_per_sample = 32;  // Good, matches first supported format list.
  ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
  format.number_of_channels = 42;  // Bad, no match.
  ASSERT_FALSE(IsDaiFormatSupported(format, formats.value()));
  format.number_of_channels = 2;  // Good.
  ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
  format.bits_per_slot = 16;  // Bad, bit per sample fo not fit.
  ASSERT_FALSE(IsDaiFormatSupported(format, formats.value()));
  ASSERT_OK(client.SetDaiFormat(std::move(format)));
  // Good, matches second supported format list.
  format = {
      .number_of_channels = 2,
      .sample_format = SAMPLE_FORMAT_PCM_SIGNED,
      .frame_format = FRAME_FORMAT_STEREO_LEFT,
      .frame_rate = 24'000,
      .bits_per_slot = 32,
      .bits_per_sample = 32,
  };
  ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
  ASSERT_OK(client.SetDaiFormat(std::move(format)));
}

TEST(SimpleCodecTest, ClientProtocolPassThroughGainPlugStates) {
  auto codec = std::make_unique<TestCodec>();
  TestCodecClient client;
  client.SetProto(codec->GetProto());

  auto formats = client.GetGainFormat();
  ASSERT_TRUE(formats.is_ok());
  ASSERT_EQ(formats->min_gain_db, -10.f);
  ASSERT_EQ(formats->max_gain_db, 10.f);
  ASSERT_EQ(formats->gain_step_db, 1.f);
  ASSERT_EQ(formats->can_mute, true);
  ASSERT_EQ(formats->can_agc, false);

  auto gain_state = client.GetGainState();
  ASSERT_TRUE(gain_state.is_ok());
  ASSERT_EQ(gain_state->gain_db, 5.f);
  ASSERT_EQ(gain_state->muted, true);
  ASSERT_EQ(gain_state->agc_enable, false);

  GainState gain_state2 = {};
  gain_state2.gain_db = kTestGain;
  client.SetGainState(gain_state2);

  auto plug_state = client.GetPlugState();
  ASSERT_TRUE(plug_state.is_ok());
  ASSERT_EQ(plug_state->hardwired, false);
  ASSERT_EQ(plug_state->plugged, true);
}

}  // namespace audio
