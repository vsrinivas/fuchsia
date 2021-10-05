// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/sync/completion.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {
static const char* kTestId = "test id";
static const char* kTestManufacturer = "test man";
static const char* kTestProduct = "test prod";
static const uint32_t kTestInstanceCount = 123;
}  // namespace
namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;

class SimpleCodecTest : public inspect::InspectTestHelper, public zxtest::Test {};

// Server tests.
struct TestCodec : public SimpleCodecServer {
  explicit TestCodec(zx_device_t* parent)
      : SimpleCodecServer(parent), proto_({&codec_protocol_ops_, this}) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx_status_t Shutdown() override { return ZX_OK; }
  zx::status<DriverIds> Initialize() override {
    return zx::ok(DriverIds{.vendor_id = 0, .device_id = 0, .instance_count = kTestInstanceCount});
  }
  zx_status_t Reset() override { return ZX_ERR_NOT_SUPPORTED; }
  Info GetInfo() override {
    return {.unique_id = kTestId, .manufacturer = kTestManufacturer, .product_name = kTestProduct};
  }
  zx_status_t Stop() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return false; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  DaiSupportedFormats GetDaiFormats() override { return {}; }
  zx::status<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  GainFormat GetGainFormat() override { return {}; }
  GainState GetGainState() override { return gain_state; }
  void SetGainState(GainState state) override { gain_state = state; }
  codec_protocol_t proto_ = {};
  inspect::Inspector& inspect() { return SimpleCodecServer::inspect(); }

  GainState gain_state = {};
};

TEST_F(SimpleCodecTest, ChannelConnection) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  ASSERT_OK(client.SetProtocol(&codec_proto));

  auto info = client.GetInfo();
  ASSERT_TRUE(info.is_ok());
  ASSERT_EQ(info->unique_id.compare(kTestId), 0);
  ASSERT_EQ(info->manufacturer.compare(kTestManufacturer), 0);
  ASSERT_EQ(info->product_name.compare(kTestProduct), 0);
}

TEST_F(SimpleCodecTest, GainState) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  ASSERT_OK(client.SetProtocol(&codec_proto));

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
}

TEST_F(SimpleCodecTest, SetDaiFormat) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  DaiFormat format = {.sample_format = audio_fidl::DaiSampleFormat::PCM_SIGNED,
                      .frame_format = FrameFormat::I2S};
  zx::status<CodecFormatInfo> codec_format_info = client.SetDaiFormat(std::move(format));
  ASSERT_EQ(codec_format_info.status_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(SimpleCodecTest, PlugState) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
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
}

TEST_F(SimpleCodecTest, Inspect) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
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
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(simple_codec->node(), "unique_id", inspect::StringPropertyValue("test id")));
}

TEST_F(SimpleCodecTest, InspectNoUniqueId) {
  struct TestCodecNoUniqueId : public TestCodec {
    explicit TestCodecNoUniqueId(zx_device_t* parent) : TestCodec(parent) {}
    zx::status<DriverIds> Initialize() override {
      return zx::ok(
          DriverIds{.vendor_id = 0, .device_id = 0, .instance_count = kTestInstanceCount});
    }
    Info GetInfo() override { return {}; }
  };
  auto fake_parent = MockDevice::FakeRootParent();

  SimpleCodecServer::CreateAndAddToDdk<TestCodecNoUniqueId>(fake_parent.get());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
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
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(simple_codec->node(), "unique_id",
                    inspect::StringPropertyValue(std::to_string(kTestInstanceCount))));
}

TEST_F(SimpleCodecTest, MultipleClients) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  SimpleCodecClient codec_clients[3];
  for (auto& codec_client : codec_clients) {
    ASSERT_OK(codec_client.SetProtocol(codec_proto2));
  }

  {
    auto state = codec_clients[0].GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, false);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 0.f);
  }

  codec_clients[1].SetGainState({.gain = 1.23f, .muted = true, .agc_enabled = false});

  // Wait for client 0 to be notified of the new gain state.
  for (;;) {
    auto state = codec_clients[0].GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->muted) {
      break;
    }
  }

  {
    auto state = codec_clients[0].GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 1.23f);
  }

  codec_clients[0].SetGainState({.gain = 5.67f, .muted = true, .agc_enabled = true});

  for (;;) {
    auto state = codec_clients[2].GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->agc_enabled) {
      break;
    }
  }

  {
    auto state = codec_clients[2].GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, true);
    EXPECT_EQ(state->gain, 5.67f);
  }
}

TEST_F(SimpleCodecTest, MoveClient) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  SimpleCodecClient codec_client1;
  ASSERT_OK(codec_client1.SetProtocol(codec_proto2));

  codec_client1.SetGainState({.gain = 1.23f, .muted = true, .agc_enabled = false});
  EXPECT_OK(codec_client1.Start());

  SimpleCodecClient codec_client2(std::move(codec_client1));

  EXPECT_NOT_OK(codec_client1.Start());  // The client was unbound, this should return an error.

  {
    auto state = codec_client2.GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 1.23f);
  }
}

TEST_F(SimpleCodecTest, CloseChannel) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  SimpleCodecClient codec_client;
  ASSERT_OK(codec_client.SetProtocol(codec_proto2));

  codec_client.SetGainState({.gain = 1.23f, .muted = true, .agc_enabled = false});

  {
    auto state = codec_client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 1.23f);
  }

  EXPECT_OK(codec_client.Start());

  // TestCodec doesn't support this, so calling it should cause the server to unbind.
  EXPECT_NOT_OK(codec_client.Stop());

  // This should fail now that our channel has been closed.
  EXPECT_NOT_OK(codec_client.Start());
}

TEST_F(SimpleCodecTest, RebindClient) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  SimpleCodecClient codec_client;
  ASSERT_OK(codec_client.SetProtocol(codec_proto2));

  codec_client.SetGainState({.gain = 1.23f, .muted = true, .agc_enabled = false});

  {
    auto state = codec_client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 1.23f);
  }

  // Do a synchronous FIDL call to flush messages on the channel and force the server to update the
  // gain state.
  EXPECT_OK(codec_client.Start());

  ASSERT_OK(codec_client.SetProtocol(codec_proto2));

  {
    auto state = codec_client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 1.23f);
  }
}

TEST_F(SimpleCodecTest, MoveClientWithDispatcherProvided) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  ASSERT_OK(loop.StartThread("SimpleCodecClient test thread"));

  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  SimpleCodecClient codec_client1(loop.dispatcher());
  ASSERT_OK(codec_client1.SetProtocol(codec_proto2));

  codec_client1.SetGainState({.gain = 1.23f, .muted = true, .agc_enabled = false});
  EXPECT_OK(codec_client1.Start());

  SimpleCodecClient codec_client2(std::move(codec_client1));

  EXPECT_NOT_OK(codec_client1.Start());  // The client was unbound, this should return an error.

  {
    auto state = codec_client2.GetGainState();
    ASSERT_TRUE(state.is_ok());
    EXPECT_EQ(state->muted, true);
    EXPECT_EQ(state->agc_enabled, false);
    EXPECT_EQ(state->gain, 1.23f);
  }
}

}  // namespace audio
