// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/sync/completion.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

namespace {
static const char* kTestId = "test id";
static const char* kTestManufacturer = "test man";
static const char* kTestProduct = "test prod";
}  // namespace
namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;

class SimpleCodecTest : public inspect::InspectTestHelper, public zxtest::Test {
 public:
  void SetUp() override {}

  void TearDown() override {}

 protected:
  fake_ddk::Bind ddk_;
};

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
  inspect::Inspector& inspect() { return SimpleCodecServer::inspect(); }
};

TEST_F(SimpleCodecTest, ChannelConnection) {
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
  ASSERT_TRUE(ddk_.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

TEST_F(SimpleCodecTest, GainState) {
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
  ASSERT_TRUE(ddk_.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

TEST_F(SimpleCodecTest, PlugState) {
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
  ASSERT_TRUE(ddk_.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

TEST_F(SimpleCodecTest, Inspect) {
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

  // Check inspect state.
  ASSERT_NO_FATAL_FAILURES(ReadInspect(codec->inspect().DuplicateVmo()));
  auto* simple_codec = hierarchy().GetByPath({"simple_codec"});
  ASSERT_TRUE(simple_codec);
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(simple_codec->node(), "state", inspect::StringPropertyValue("created")));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(simple_codec->node(), "start_time", inspect::IntPropertyValue(0)));

  codec->DdkAsyncRemove();
  ASSERT_TRUE(ddk_.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

TEST_F(SimpleCodecTest, MultipleClients) {
  auto codec = SimpleCodecServer::Create<TestCodec>();
  ASSERT_NOT_NULL(codec);
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  audio_fidl::CodecSyncPtr codec_clients[3];
  for (auto& codec_client : codec_clients) {
    zx::channel channel_remote, channel_local;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ASSERT_OK(codec_proto2.Connect(std::move(channel_remote)));
    codec_client.Bind(std::move(channel_local));
  }

  audio_fidl::CodecInfo info;
  ASSERT_OK(codec_clients[0]->GetInfo(&info));
  EXPECT_EQ(info.unique_id, std::string(kTestId));

  ASSERT_OK(codec_clients[1]->GetInfo(&info));
  EXPECT_EQ(info.unique_id, std::string(kTestId));

  ASSERT_OK(codec_clients[2]->GetInfo(&info));
  EXPECT_EQ(info.unique_id, std::string(kTestId));

  codec->DdkAsyncRemove();
  ASSERT_TRUE(ddk_.Ok());
  codec.release()->DdkRelease();  // codec release managed by the DDK
}

}  // namespace audio
