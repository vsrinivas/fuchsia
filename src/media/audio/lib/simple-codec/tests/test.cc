// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

namespace {
static const char* kTestId = "test id";
static const char* kTestManufacturer = "test man";
static const char* kTestProduct = "test prod";
}  // namespace
namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;

// Server tests.
struct TestCodec : public SimpleCodecServer {
  explicit TestCodec()
      : SimpleCodecServer(fake_ddk::kFakeParent), proto_({&codec_protocol_ops_, this}) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx_status_t Shutdown() override { return ZX_OK; }
  zx::status<DriverIds> Initialize() override {
    return zx::ok(DriverIds{.vendor_id = 0, .device_id = 0});
  }
  zx_status_t Reset() override { return ZX_ERR_NOT_SUPPORTED; }
  Info GetInfo() override {
    return {.unique_id = kTestId, .manufacturer = kTestManufacturer, .product_name = kTestProduct};
  }
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_ERR_NOT_SUPPORTED; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  DaiSupportedFormats GetDaiFormats() override { return {}; }
  zx_status_t SetDaiFormat(const DaiFormat& format) override { return ZX_ERR_NOT_SUPPORTED; }
  GainFormat GetGainFormat() override { return {}; }
  GainState GetGainState() override { return {}; }
  void SetGainState(GainState state) override {}
  codec_protocol_t proto_ = {};
};

TEST(SimpleCodecTest, ChannelConnection) {
  fake_ddk::Bind tester;
  auto codec = SimpleCodecServer::Create<TestCodec>();
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto info = client.GetInfo();
  ASSERT_TRUE(info.is_ok());
  ASSERT_EQ(info->unique_id.compare(kTestId), 0);
  ASSERT_EQ(info->manufacturer.compare(kTestManufacturer), 0);
  ASSERT_EQ(info->product_name.compare(kTestProduct), 0);

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

TEST(SimpleCodecTest, GainState) {
  fake_ddk::Bind tester;
  auto codec = SimpleCodecServer::Create<TestCodec>();
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // Defaults to false/0db.
  {
    auto state = client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    ASSERT_EQ(state->muted, false);
    ASSERT_EQ(state->agc_enabled, false);
    ASSERT_EQ(state->gain, 0.f);
  }

  // Still all set to false/0db.
  {
    auto state = client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    ASSERT_EQ(state->muted, false);
    ASSERT_EQ(state->agc_enabled, false);
    ASSERT_EQ(state->gain, 0.f);
  }

  // Set gain now.
  client.SetGainState({.gain = 1.23f, .muted = true, .agc_enabled = true});

  // Values updated now.
  {
    auto state = client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    ASSERT_EQ(state->muted, true);
    ASSERT_EQ(state->agc_enabled, true);
    ASSERT_EQ(state->gain, 1.23f);
  }

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

TEST(SimpleCodecTest, PlugState) {
  fake_ddk::Bind tester;
  auto codec = SimpleCodecServer::Create<TestCodec>();
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  zx::channel channel_remote, channel_local;
  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
  ddk::CodecProtocolClient proto_client;
  ASSERT_OK(codec_proto2.Connect(std::move(channel_remote)));
  audio_fidl::CodecSyncPtr codec_client;
  codec_client.Bind(std::move(channel_local));

  audio_fidl::PlugDetectCapabilities out_plug_detect_capabilites;
  ASSERT_OK(codec_client->GetPlugDetectCapabilities(&out_plug_detect_capabilites));
  ASSERT_EQ(out_plug_detect_capabilites, audio_fidl::PlugDetectCapabilities::HARDWIRED);

  audio_fidl::PlugState out_plug_state;
  ASSERT_OK(codec_client->WatchPlugState(&out_plug_state));
  ASSERT_EQ(out_plug_state.plugged(), true);
  ASSERT_GT(out_plug_state.plug_state_time(), 0);

  codec->DdkAsyncRemove();
  ASSERT_TRUE(tester.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

}  // namespace audio
