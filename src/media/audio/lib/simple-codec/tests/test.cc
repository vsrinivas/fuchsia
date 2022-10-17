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
namespace signal_fidl = ::fuchsia::hardware::audio::signalprocessing;

class SimpleCodecTest : public inspect::InspectTestHelper, public zxtest::Test {};

// Server tests.
class TestCodec : public SimpleCodecServer {
 public:
  explicit TestCodec(zx_device_t* parent) : SimpleCodecServer(parent) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
  zx_status_t Shutdown() override { return ZX_OK; }
  zx::result<DriverIds> Initialize() override {
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
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  GainFormat GetGainFormat() override {
    return GainFormat{
        .can_mute = true,
        .can_agc = true,
    };
  }
  GainState GetGainState() override { return gain_state_; }
  void SetGainState(GainState state) override { gain_state_ = state; }
  inspect::Inspector& inspect() { return SimpleCodecServer::inspect(); }
  uint64_t GetTopologyId() { return SimpleCodecServer::GetTopologyId(); }
  uint64_t GetGainPeId() { return SimpleCodecServer::GetGainPeId(); }
  uint64_t GetMutePeId() { return SimpleCodecServer::GetMutePeId(); }
  uint64_t GetAgcPeId() { return SimpleCodecServer::GetAgcPeId(); }

 private:
  GainState gain_state_ = {};
};

class TestCodecWithSignalProcessing : public SimpleCodecServer,
                                      public signal_fidl::SignalProcessing {
 public:
  explicit TestCodecWithSignalProcessing(zx_device_t* parent) : SimpleCodecServer(parent) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx_status_t Shutdown() override { return ZX_OK; }
  zx::result<DriverIds> Initialize() override {
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
  bool SupportsSignalProcessing() override { return true; }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing) override {
    signal_processing_binding_.emplace(this, std::move(signal_processing), dispatcher());
  }
  void GetElements(GetElementsCallback callback) override {
    signal_fidl::Element pe;
    pe.set_id(kAglPeId);
    pe.set_type(signal_fidl::ElementType::AUTOMATIC_GAIN_LIMITER);

    std::vector<signal_fidl::Element> pes;
    pes.emplace_back(std::move(pe));
    signal_fidl::Reader_GetElements_Response response(std::move(pes));
    signal_fidl::Reader_GetElements_Result result;
    result.set_response(std::move(response));
    callback(std::move(result));
  }
  void SetElementState(uint64_t processing_element_id, signal_fidl::ElementState state,
                       SetElementStateCallback callback) override {
    ASSERT_EQ(processing_element_id, kAglPeId);
    ASSERT_TRUE(state.has_enabled());
    agl_mode_ = state.enabled();
    callback(signal_fidl::SignalProcessing_SetElementState_Result::WithResponse(
        signal_fidl::SignalProcessing_SetElementState_Response()));
  }
  void WatchElementState(uint64_t processing_element_id,
                         WatchElementStateCallback callback) override {}
  void GetTopologies(GetTopologiesCallback callback) override {
    signal_fidl::EdgePair edge;
    edge.processing_element_id_from = kAglPeId;
    edge.processing_element_id_to = kAglPeId;

    std::vector<signal_fidl::EdgePair> edges;
    edges.emplace_back(edge);

    signal_fidl::Topology topology;
    topology.set_id(kTopologyId);
    topology.set_processing_elements_edge_pairs(edges);

    std::vector<signal_fidl::Topology> topologies;
    topologies.emplace_back(std::move(topology));

    signal_fidl::Reader_GetTopologies_Response response(std::move(topologies));
    signal_fidl::Reader_GetTopologies_Result result;
    result.set_response(std::move(response));
    callback(std::move(result));
  }
  void SetTopology(uint64_t topology_id, SetTopologyCallback callback) override {
    if (topology_id != kTopologyId) {
      callback(signal_fidl::SignalProcessing_SetTopology_Result::WithErr(ZX_ERR_INVALID_ARGS));
      return;
    }
    callback(signal_fidl::SignalProcessing_SetTopology_Result::WithResponse(
        signal_fidl::SignalProcessing_SetTopology_Response()));
  }
  DaiSupportedFormats GetDaiFormats() override { return {}; }
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  GainFormat GetGainFormat() override { return {}; }
  GainState GetGainState() override { return gain_state_; }
  void SetGainState(GainState state) override { gain_state_ = state; }

  bool agl_mode() { return agl_mode_; }
  inspect::Inspector& inspect() { return SimpleCodecServer::inspect(); }

 private:
  static constexpr uint64_t kAglPeId = 1;
  static constexpr uint64_t kTopologyId = 1;
  GainState gain_state_ = {};
  bool agl_mode_ = false;
  std::optional<fidl::Binding<signal_fidl::SignalProcessing>> signal_processing_binding_;
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

  // Values updated eventually.
  for (;;) {
    auto state = client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->muted && state->agc_enabled && state->gain == 1.23f) {
      break;
    }
  }
}

TEST_F(SimpleCodecTest, DefaultTopology) {
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

  fidl::InterfaceHandle<signal_fidl::SignalProcessing> signal_processing_handle;
  fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing_request =
      signal_processing_handle.NewRequest();
  ASSERT_OK(codec_client->SignalProcessingConnect(std::move(signal_processing_request)));
  fidl::SynchronousInterfacePtr signal_processing_client = signal_processing_handle.BindSync();

  // We should get 3 PEs with gain, mute and AGC support.
  {
    signal_fidl::Reader_GetElements_Result result;
    ASSERT_OK(signal_processing_client->GetElements(&result));
    ASSERT_FALSE(result.is_err());
    ASSERT_EQ(result.response().processing_elements.size(), 3);
    ASSERT_EQ(result.response().processing_elements[0].type(), signal_fidl::ElementType::GAIN);
    ASSERT_EQ(result.response().processing_elements[1].type(), signal_fidl::ElementType::MUTE);
    ASSERT_EQ(result.response().processing_elements[2].type(),
              signal_fidl::ElementType::AUTOMATIC_GAIN_CONTROL);
  }

  // Only one topology.
  {
    signal_fidl::Reader_GetTopologies_Result result;
    ASSERT_OK(signal_processing_client->GetTopologies(&result));
    ASSERT_FALSE(result.is_err());
    ASSERT_EQ(result.response().topologies.size(), 1);
    ASSERT_EQ(result.response().topologies[0].id(), codec->GetTopologyId());
    ASSERT_EQ(result.response().topologies[0].processing_elements_edge_pairs().size(), 2);
    ASSERT_EQ(result.response()
                  .topologies[0]
                  .processing_elements_edge_pairs()[0]
                  .processing_element_id_from,
              codec->GetGainPeId());
    ASSERT_EQ(result.response()
                  .topologies[0]
                  .processing_elements_edge_pairs()[0]
                  .processing_element_id_to,
              codec->GetMutePeId());
    ASSERT_EQ(result.response()
                  .topologies[0]
                  .processing_elements_edge_pairs()[1]
                  .processing_element_id_from,
              codec->GetMutePeId());
    ASSERT_EQ(result.response()
                  .topologies[0]
                  .processing_elements_edge_pairs()[1]
                  .processing_element_id_to,
              codec->GetAgcPeId());
  }

  // Set the only topology must work.
  {
    signal_fidl::SignalProcessing_SetTopology_Result result;
    ASSERT_OK(signal_processing_client->SetTopology(codec->GetTopologyId(), &result));
    ASSERT_FALSE(result.is_err());
  }

  // Set the an incorrect topology id must fail.
  {
    signal_fidl::SignalProcessing_SetTopology_Result result;
    ASSERT_OK(signal_processing_client->SetTopology(codec->GetTopologyId() + 1, &result));
    ASSERT_TRUE(result.is_err());
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
  zx::result<CodecFormatInfo> codec_format_info = client.SetDaiFormat(std::move(format));
  ASSERT_EQ(codec_format_info.status_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(SimpleCodecTest, PlugStateHardwired) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodec>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodec>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  // First client.
  {
    zx::channel channel_remote, channel_local;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ddk::CodecProtocolClient proto_client;
    ASSERT_OK(codec_proto2.Connect(std::move(channel_remote)));
    audio_fidl::CodecSyncPtr codec_client;
    codec_client.Bind(std::move(channel_local));

    audio_fidl::PlugDetectCapabilities out_plug_detect_capabilities;
    ASSERT_OK(codec_client->GetPlugDetectCapabilities(&out_plug_detect_capabilities));
    ASSERT_EQ(out_plug_detect_capabilities, audio_fidl::PlugDetectCapabilities::HARDWIRED);
    audio_fidl::PlugState out_plug_state;
    ASSERT_OK(codec_client->WatchPlugState(&out_plug_state));
    ASSERT_EQ(out_plug_state.plugged(), true);
    ASSERT_GT(out_plug_state.plug_state_time(), 0);
  }
  // Second client.
  {
    zx::channel channel_remote, channel_local;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ddk::CodecProtocolClient proto_client;
    ASSERT_OK(codec_proto2.Connect(std::move(channel_remote)));
    audio_fidl::CodecSyncPtr codec_client;
    codec_client.Bind(std::move(channel_local));

    audio_fidl::PlugDetectCapabilities out_plug_detect_capabilities;
    ASSERT_OK(codec_client->GetPlugDetectCapabilities(&out_plug_detect_capabilities));
    ASSERT_EQ(out_plug_detect_capabilities, audio_fidl::PlugDetectCapabilities::HARDWIRED);
    audio_fidl::PlugState out_plug_state;
    ASSERT_OK(codec_client->WatchPlugState(&out_plug_state));
    ASSERT_EQ(out_plug_state.plugged(), true);
    ASSERT_GT(out_plug_state.plug_state_time(), 0);
  }
}

TEST_F(SimpleCodecTest, AglStateServerWithClientViaSignalProcessingApi) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<TestCodecWithSignalProcessing>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<TestCodecWithSignalProcessing>();
  auto codec_proto = codec->GetProto();
  ddk::CodecProtocolClient codec_proto2(&codec_proto);

  zx::channel channel_remote, channel_local;
  ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
  ddk::CodecProtocolClient proto_client;
  ASSERT_OK(codec_proto2.Connect(std::move(channel_remote)));
  audio_fidl::CodecSyncPtr codec_client;
  codec_client.Bind(std::move(channel_local));

  fidl::InterfaceHandle<signal_fidl::SignalProcessing> signal_processing_handle;
  fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing_request =
      signal_processing_handle.NewRequest();
  ASSERT_OK(codec_client->SignalProcessingConnect(std::move(signal_processing_request)));
  fidl::SynchronousInterfacePtr signal_processing_client = signal_processing_handle.BindSync();

  // We should get one PE with AGL support.
  signal_fidl::Reader_GetElements_Result result;
  ASSERT_OK(signal_processing_client->GetElements(&result));
  ASSERT_FALSE(result.is_err());
  ASSERT_EQ(result.response().processing_elements.size(), 1);
  ASSERT_EQ(result.response().processing_elements[0].type(),
            signal_fidl::ElementType::AUTOMATIC_GAIN_LIMITER);
  ASSERT_FALSE(codec->agl_mode());

  // Control with enabled = true.
  signal_fidl::SignalProcessing_SetElementState_Result result_enable;
  signal_fidl::ElementState state_enable;
  state_enable.set_enabled(true);
  ASSERT_OK(signal_processing_client->SetElementState(result.response().processing_elements[0].id(),
                                                      std::move(state_enable), &result_enable));
  ASSERT_FALSE(result_enable.is_err());
  ASSERT_TRUE(codec->agl_mode());
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
  ASSERT_NO_FATAL_FAILURE(ReadInspect(codec->inspect().DuplicateVmo()));
  auto* simple_codec = hierarchy().GetByPath({"simple_codec"});
  ASSERT_TRUE(simple_codec);
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "state", inspect::StringPropertyValue("created")));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "start_time", inspect::IntPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "unique_id", inspect::StringPropertyValue("test id")));
}

TEST_F(SimpleCodecTest, InspectNoUniqueId) {
  struct TestCodecNoUniqueId : public TestCodec {
    explicit TestCodecNoUniqueId(zx_device_t* parent) : TestCodec(parent) {}
    zx::result<DriverIds> Initialize() override {
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
  ASSERT_NO_FATAL_FAILURE(ReadInspect(codec->inspect().DuplicateVmo()));
  auto* simple_codec = hierarchy().GetByPath({"simple_codec"});
  ASSERT_TRUE(simple_codec);
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "state", inspect::StringPropertyValue("created")));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "start_time", inspect::IntPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURE(
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

  // Values updated eventually.
  for (;;) {
    auto state = codec_client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->muted && !state->agc_enabled && state->gain == 1.23f) {
      break;
    }
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

  // Values updated eventually.
  for (;;) {
    auto state = codec_client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->muted && !state->agc_enabled && state->gain == 1.23f) {
      break;
    }
  }

  // Do a synchronous FIDL call to flush messages on the channel and force the server to update the
  // gain state.
  EXPECT_OK(codec_client.Start());

  ASSERT_OK(codec_client.SetProtocol(codec_proto2));

  // Values updated eventually.
  for (;;) {
    auto state = codec_client.GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->muted && !state->agc_enabled && state->gain == 1.23f) {
      break;
    }
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

  // Values updated eventually.
  for (;;) {
    auto state = codec_client2.GetGainState();
    ASSERT_TRUE(state.is_ok());
    if (state->muted && !state->agc_enabled && state->gain == 1.23f) {
      break;
    }
  }
}

}  // namespace audio
