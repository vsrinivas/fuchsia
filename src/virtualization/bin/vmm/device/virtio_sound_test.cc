// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <array>
#include <deque>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gmock/gmock.h>
#include <virtio/sound.h>

#include "fuchsia/logger/cpp/fidl.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

#define UNEXPECTED_METHOD_CALL ADD_FAILURE() << "unexpected method call " << __func__

namespace {

class FakeAudioRenderer : public fuchsia::media::AudioRenderer {
 public:
  FakeAudioRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
                    async_dispatcher_t* dispatcher)
      : binding_(this, std::move(request), dispatcher) {
    binding_.set_error_handler([this](zx_status_t status) {
      calls_.push_back({.method = Method::Disconnect, .disconnect_status = status});
    });
  }

  // FakeAudioRenderer records every method call using the following types.
  // SendPacket is tracked separately, by the Packet type.
  enum class Method {
    AddPayloadBuffer,
    SetUsage,
    SetPcmStreamType,
    EnableMinLeadTimeEvents,
    Play,
    PauseNoReply,
    DiscardAllPackets,
    Disconnect,
  };

  struct Call {
    Method method;
    // AddPayloadBuffer
    size_t payload_buffer_size;
    // SetPcmStreamType
    fuchsia::media::AudioStreamType stream_type;
    // Disconnect
    zx_status_t disconnect_status;
  };

  std::deque<Call>& calls() { return calls_; }

  struct Packet {
    std::string buffer;
    SendPacketCallback callback;
  };

  std::deque<Packet>& packets() { return packets_; }

  // Generate an OnMinLeadTimeChanged event.
  // This is automatically called with kDefaultMinLeadTime when the user calls
  // EnableMinLeadTimeEvents.
  void ReportMinLeadTime(zx::duration lead_time) {
    binding_.events().OnMinLeadTimeChanged(lead_time.to_nsecs());
  }

  // Arbitrary.
  static constexpr auto kDefaultMinLeadTime = zx::msec(10);

 protected:
  // Expected methods.
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) override {
    EXPECT_EQ(id, 0u);
    ASSERT_TRUE(payload_buffer.is_valid());
    size_t size;
    ASSERT_EQ(payload_buffer.get_size(&size), ZX_OK);
    calls_.push_back({.method = Method::AddPayloadBuffer, .payload_buffer_size = size});
    ASSERT_EQ(ZX_OK, payload_mapper_.Map(std::move(payload_buffer)));
  }
  void SetUsage(fuchsia::media::AudioRenderUsage usage) override {
    EXPECT_EQ(usage, fuchsia::media::AudioRenderUsage::MEDIA);
    calls_.push_back({.method = Method::SetUsage});
  }
  void SetPcmStreamType(fuchsia::media::AudioStreamType type) override {
    calls_.push_back({.method = Method::SetPcmStreamType, .stream_type = type});
  }
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) override {
    EXPECT_EQ(reference_time, fuchsia::media::NO_TIMESTAMP);
    EXPECT_EQ(media_time, fuchsia::media::NO_TIMESTAMP);
    calls_.push_back({.method = Method::Play});
    callback(0, 0);  // arbitrary: these values are unused by the virtio-sound device
  }
  void PauseNoReply() override { calls_.push_back({.method = Method::PauseNoReply}); }
  void DiscardAllPackets(DiscardAllPacketsCallback callback) override {
    calls_.push_back({.method = Method::DiscardAllPackets});
    callback();
  }
  void EnableMinLeadTimeEvents(bool enabled) override {
    EXPECT_TRUE(enabled);
    calls_.push_back({.method = Method::EnableMinLeadTimeEvents});
    ReportMinLeadTime(kDefaultMinLeadTime);
  }
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) override {
    EXPECT_EQ(packet.pts, fuchsia::media::NO_TIMESTAMP);
    EXPECT_EQ(packet.payload_buffer_id, 0u);
    EXPECT_EQ(packet.flags, 0u);
    ASSERT_LT(packet.payload_offset, payload_mapper_.size());
    ASSERT_LE(packet.payload_offset + packet.payload_size, payload_mapper_.size());
    char* start = reinterpret_cast<char*>(payload_mapper_.start()) + packet.payload_offset;
    char* end = start + packet.payload_size;
    packets_.push_back({.buffer = std::string(start, end), .callback = std::move(callback)});
  }

  // Unexpected methods.
  void RemovePayloadBuffer(uint32_t id) override { UNEXPECTED_METHOD_CALL; }
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) override { UNEXPECTED_METHOD_CALL; }
  void EndOfStream() override { UNEXPECTED_METHOD_CALL; }
  void DiscardAllPacketsNoReply() override { UNEXPECTED_METHOD_CALL; }
  void SetPtsUnits(uint32_t tick_per_second_numerator,
                   uint32_t tick_per_second_denominator) override {
    UNEXPECTED_METHOD_CALL;
  }
  void SetPtsContinuityThreshold(float threshold_seconds) override { UNEXPECTED_METHOD_CALL; }
  void GetReferenceClock(GetReferenceClockCallback callback) override { UNEXPECTED_METHOD_CALL; }
  void SetReferenceClock(::zx::clock reference_clock) override { UNEXPECTED_METHOD_CALL; }
  void PlayNoReply(int64_t reference_time, int64_t media_time) override { UNEXPECTED_METHOD_CALL; }
  void Pause(PauseCallback callback) override { UNEXPECTED_METHOD_CALL; }
  void GetMinLeadTime(GetMinLeadTimeCallback callback) override { UNEXPECTED_METHOD_CALL; }
  void BindGainControl(::fidl::InterfaceRequest<::fuchsia::media::audio::GainControl>
                           gain_control_request) override {
    UNEXPECTED_METHOD_CALL;
  }

 private:
  fidl::Binding<fuchsia::media::AudioRenderer> binding_;
  fzl::VmoMapper payload_mapper_;
  std::deque<Call> calls_;
  std::deque<Packet> packets_;
};

class FakeAudioCapturer : public fuchsia::media::AudioCapturer {
 public:
  FakeAudioCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                    async_dispatcher_t* dispatcher)
      : binding_(this, std::move(request), dispatcher) {
    binding_.set_error_handler([this](zx_status_t status) {
      calls_.push_back({.method = Method::Disconnect, .disconnect_status = status});
    });
  }

  // FakeAudioCapturer records every method call using the following types.
  // CaptureAt is tracked separately, by the Packet type.
  enum class Method {
    AddPayloadBuffer,
    SetUsage,
    SetPcmStreamType,
    Disconnect,
  };

  struct Call {
    Method method;
    // AddPayloadBuffer
    size_t payload_buffer_size;
    // SetPcmStreamType
    fuchsia::media::AudioStreamType stream_type;
    // Disconnect
    zx_status_t disconnect_status;
  };

  std::deque<Call>& calls() { return calls_; }

  struct Packet {
    uint32_t payload_offset_frames;
    uint32_t payload_size_frames;
    CaptureAtCallback callback;
  };

  std::deque<Packet>& packets() { return packets_; }

  void ReleasePacket(const Packet& packet, const std::string& data, int64_t bytes_per_frame,
                     zx::duration packet_latency) {
    uint64_t payload_offset_bytes = packet.payload_offset_frames * bytes_per_frame;
    ASSERT_LT(payload_offset_bytes, payload_mapper_.size());
    ASSERT_LE(payload_offset_bytes + data.size(), payload_mapper_.size());

    uint64_t payload_size_bytes = packet.payload_size_frames * bytes_per_frame;
    ASSERT_EQ(data.size(), payload_size_bytes);

    char* start = reinterpret_cast<char*>(payload_mapper_.start()) + payload_offset_bytes;
    memmove(start, data.data(), data.size());
    packet.callback(fuchsia::media::StreamPacket{
        .pts = (zx::clock::get_monotonic() - packet_latency).get(),
        .payload_buffer_id = 0,
        .payload_offset = payload_offset_bytes,
        .payload_size = data.size(),
    });
  }

 protected:
  // Expected methods.
  void AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) override {
    EXPECT_EQ(id, 0u);
    ASSERT_TRUE(payload_buffer.is_valid());
    size_t size;
    ASSERT_EQ(payload_buffer.get_size(&size), ZX_OK);
    calls_.push_back({.method = Method::AddPayloadBuffer, .payload_buffer_size = size});
    ASSERT_EQ(ZX_OK, payload_mapper_.Map(std::move(payload_buffer)));
  }
  void SetUsage(fuchsia::media::AudioCaptureUsage usage) override {
    EXPECT_EQ(usage, fuchsia::media::AudioCaptureUsage::FOREGROUND);
    calls_.push_back({.method = Method::SetUsage});
  }
  void SetPcmStreamType(fuchsia::media::AudioStreamType type) override {
    calls_.push_back({.method = Method::SetPcmStreamType, .stream_type = type});
  }
  void CaptureAt(uint32_t payload_buffer_id, uint32_t payload_offset_frames, uint32_t frames,
                 CaptureAtCallback callback) override {
    EXPECT_EQ(payload_buffer_id, 0u);
    packets_.push_back({
        .payload_offset_frames = payload_offset_frames,
        .payload_size_frames = frames,
        .callback = std::move(callback),
    });
  }

  void RemovePayloadBuffer(uint32_t id) override { UNEXPECTED_METHOD_CALL; }
  void ReleasePacket(fuchsia::media::StreamPacket packet) override { UNEXPECTED_METHOD_CALL; }
  void DiscardAllPackets(DiscardAllPacketsCallback callback) override { UNEXPECTED_METHOD_CALL; }
  void DiscardAllPacketsNoReply() override { UNEXPECTED_METHOD_CALL; }
  void StartAsyncCapture(uint32_t frames_per_packet) override { UNEXPECTED_METHOD_CALL; }
  void StopAsyncCapture(StopAsyncCaptureCallback callback) override { UNEXPECTED_METHOD_CALL; }
  void StopAsyncCaptureNoReply() override { UNEXPECTED_METHOD_CALL; }
  void BindGainControl(::fidl::InterfaceRequest<::fuchsia::media::audio::GainControl>
                           gain_control_request) override {
    UNEXPECTED_METHOD_CALL;
  }
  void GetReferenceClock(GetReferenceClockCallback callback) override { UNEXPECTED_METHOD_CALL; }
  void SetReferenceClock(::zx::clock ref_clock) override { UNEXPECTED_METHOD_CALL; }
  void GetStreamType(GetStreamTypeCallback callback) override { UNEXPECTED_METHOD_CALL; }

 private:
  fidl::Binding<fuchsia::media::AudioCapturer> binding_;
  fzl::VmoMapper payload_mapper_;
  std::deque<Call> calls_;
  std::deque<Packet> packets_;
};

class FakeAudio : public fuchsia::media::Audio, public component_testing::LocalComponent {
 public:
  explicit FakeAudio(async::Loop& loop) : loop_(loop) {}

  void CreateAudioRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request) final {
    renderers_.push_back(
        std::make_unique<FakeAudioRenderer>(std::move(request), loop_.dispatcher()));
  }
  void CreateAudioCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                           bool loopback) final {
    EXPECT_FALSE(loopback);
    capturers_.push_back(
        std::make_unique<FakeAudioCapturer>(std::move(request), loop_.dispatcher()));
  }

  void Start(std::unique_ptr<component_testing::LocalComponentHandles> handles) override {
    // This class contains handles to the component's incoming and outgoing capabilities.
    handles_ = std::move(handles);

    ASSERT_EQ(
        handles_->outgoing()->AddPublicService(binding_set_.GetHandler(this, loop_.dispatcher())),
        ZX_OK);
  }

  bool HasStarted() const { return handles_ != nullptr; }

  std::vector<std::unique_ptr<FakeAudioRenderer>> renderers_;
  std::vector<std::unique_ptr<FakeAudioCapturer>> capturers_;

 private:
  async::Loop& loop_;
  fidl::BindingSet<fuchsia::media::Audio> binding_set_;
  std::unique_ptr<component_testing::LocalComponentHandles> handles_;
};

uint64_t bit(uint64_t n) { return 1ul << n; }

enum QueueId {
  CONTROLQ = 0,
  EVENTQ = 1,
  TXQ = 2,
  RXQ = 3,
};

static const uint32_t kNumJacks = 1;
static const uint32_t kNumStreams = 2;
static const uint32_t kNumChmaps = 3;

static const uint32_t kOutputStreamId = 0;
static const uint32_t kInputStreamId = 1;

static const auto kDeadlinePeriod = zx::msec(5);

// Each response struct contains a status. We initialize that response status
// to this value when we want to verify that the response is not written before
// a certain point in time.
static constexpr uint32_t kInvalidStatus = 0xffff;
static_assert(VIRTIO_SND_S_OK != kInvalidStatus);

struct QueueConfig {
  uint16_t descriptors;
  size_t data_bytes;
};

static constexpr QueueConfig kQueueConfigs[4] = {
    {16, 16 * 128},  // all req+resp messages are < 128 bytes
    {16, 16 * 64},   // all messages are < 64 bytes
    {16, PAGE_SIZE},
    {16, PAGE_SIZE},
};

constexpr auto kTimeout = zx::sec(20);

template <bool EnableInput>
class VirtioSoundTestBase : public TestWithDevice {
 protected:
  VirtioSoundTestBase() : audio_service_(loop()) {
    zx_gpaddr_t addr = 0;
    for (int k = 0; k < 4; k++) {
      queue_data_addrs_[k] = addr;
      addr += kQueueConfigs[k].data_bytes;
      queues_[k] = std::make_unique<VirtioQueueFake>(phys_mem_, addr, kQueueConfigs[k].descriptors);
      addr = queues_[k]->end();
    }
    phys_mem_size_ = addr;
  }

  void SetUp() override {
    constexpr auto kComponentUrl = "fuchsia-pkg://fuchsia.com/virtio_sound#meta/virtio_sound.cm";
    constexpr auto kComponentName = "virtio_sound";
    constexpr auto kFakeAudio = "fake_audio";

    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, kComponentUrl);
    realm_builder.AddLocalChild(kFakeAudio, &audio_service_);

    realm_builder
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::logger::LogSink::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                                Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::media::Audio::Name_},
                            },
                        .source = {ChildRef{kFakeAudio}},
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::virtualization::hardware::VirtioSound::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(phys_mem_size_, &start_info);
    ASSERT_EQ(ZX_OK, status);

    sound_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioSound>();

    uint32_t features, jacks, streams, chmaps;
    ASSERT_EQ(ZX_OK,
              sound_->Start(std::move(start_info), EnableInput, true /* enable_verbose_logging */,
                            &features, &jacks, &streams, &chmaps));

    ASSERT_EQ(features, 0u);
    ASSERT_EQ(jacks, kNumJacks);
    if (EnableInput) {
      ASSERT_EQ(streams, kNumStreams);
    } else {
      ASSERT_EQ(streams, 1u);
    }
    ASSERT_EQ(chmaps, kNumChmaps);

    // Configure device queues.
    for (uint16_t k = 0; k < queues_.size(); k++) {
      SCOPED_TRACE(fxl::StringPrintf("queue %u", k));
      auto& q = *queues_[k];
      q.Configure(queue_data_addrs_[k], kQueueConfigs[k].data_bytes);
      ASSERT_EQ(ZX_OK, sound_->ConfigureQueue(k, q.size(), q.desc(), q.avail(), q.used()));
    }

    // Finish negotiating features.
    ASSERT_EQ(ZX_OK, sound_->Ready(0));

    // Wait until virtio_sound has connected to the mock object
    RunLoopWithTimeoutOrUntil([&]() { return audio_service_.HasStarted(); }, kTimeout);
  }

  FakeAudioRenderer* get_audio_renderer(size_t k) {
    if (RunLoopWithTimeoutOrUntil([&]() { return audio_service_.renderers_.size() > k; },
                                  kTimeout)) {
      return audio_service_.renderers_[k].get();
    }

    return nullptr;
  }

  FakeAudioCapturer* get_audio_capturer(size_t k) {
    if (RunLoopWithTimeoutOrUntil([&]() { return audio_service_.capturers_.size() > k; },
                                  kTimeout)) {
      return audio_service_.capturers_[k].get();
    }

    return nullptr;
  }

  VirtioQueueFake& controlq() { return *queues_[CONTROLQ]; }
  VirtioQueueFake& eventq() { return *queues_[EVENTQ]; }
  VirtioQueueFake& txq() { return *queues_[TXQ]; }
  VirtioQueueFake& rxq() { return *queues_[RXQ]; }

  zx_status_t NotifyQueue(QueueId id) { return sound_->NotifyQueue(id); }

  // Send a message on controlq with the given type and check the response code.
  // Assumes the response is a simple virtio_snd_hdr.
  template <class T>
  void CheckSimpleCall(uint32_t expected_resp_code, T msg) {
    virtio_snd_hdr* resphdr;
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                         .AppendReadableDescriptor(&msg, sizeof(msg))
                         .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                         .Build());
    ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
    ASSERT_EQ(ZX_OK, WaitOnInterrupt());
    ASSERT_EQ(resphdr->code, expected_resp_code);
  }

  // Helper that waits for each method to be called exactly once (in any order), then
  // returns info about each call. Can be used with FakeAudio{Renderer,Capturer}.
  // On timeout, returns an empty set.
  template <class Server>
  std::unordered_map<typename Server::Method, typename Server::Call>  //
  WaitForCalls(Server& server, std::set<typename Server::Method> expected_methods) {
    std::unordered_map<typename Server::Method, typename Server::Call> out;
    while (!expected_methods.empty()) {
      RunLoopWithTimeoutOrUntil([&server]() { return !server.calls().empty(); }, kTimeout);
      if (server.calls().empty()) {
        std::ostringstream os;
        os << "Timeout waiting for method calls: need {";
        for (auto m : expected_methods) {
          os << static_cast<int>(m) << ", ";
        }
        os << "}, have {";
        for (auto [m, c] : out) {
          os << static_cast<int>(m) << ", ";
        }
        os << "}";
        ADD_FAILURE() << os.str();
        return {};
      }

      auto call = server.calls().front();
      if (!expected_methods.count(call.method)) {
        ADD_FAILURE() << "Got unexpected method call " << static_cast<int>(call.method);
        return {};
      }
      if (out.count(call.method)) {
        ADD_FAILURE() << "Got duplicate method call " << static_cast<int>(call.method);
        return {};
      }
      out[call.method] = call;

      server.calls().pop_front();
      expected_methods.erase(call.method);
    }

    return out;
  }

  // Helper that waits for the given number of packets to arrive. Can be used with
  // FakeAudio{Renderer,Capturer}. On timeout, returns an empty set.
  template <class Server>
  std::deque<typename Server::Packet> WaitForPackets(Server& server, size_t expected_count) {
    RunLoopWithTimeoutOrUntil(
        [&server, expected_count]() { return server.packets().size() == expected_count; },
        kTimeout);
    if (server.packets().empty()) {
      ADD_FAILURE() << "Timed out waiting for " << expected_count << " packets; got "
                    << server.packets().size();
      return {};
    }
    return std::move(server.packets());
  }

  // Helper that waits until the given descriptor index moves into the "used" ring,
  // which signals that the request has completed.
  //
  // This is useful in tests where multiple requests may complete concurrently. If
  // at most one test can complete at a time, it's sufficient to call WaitOnInterrupt()
  // to wait for that request to complete.
  zx_status_t WaitForDescriptor(VirtioQueueFake& queue, uint32_t idx) {
    auto& used = used_descriptors_[&queue];
    while (!used.count(idx)) {
      auto elem = queue.NextUsed();
      while (!elem) {
        if (auto status = WaitOnInterrupt(); status != ZX_OK) {
          return status;
        }
        elem = queue.NextUsed();
      }
      used.insert(elem->id);
    }
    return ZX_OK;
  }

  void TestPcmOutputSetParamsAndPrepare(uint8_t channels, uint8_t format, uint8_t rate,
                                        fuchsia::media::AudioSampleFormat fidl_format,
                                        uint32_t fidl_rate, uint32_t buffer_bytes = 1024,
                                        uint32_t period_bytes = 64);
  void TestPcmOutputStateTraversal(size_t renderer_id);
  void TestPcmBadTransition(uint32_t stream_id, std::vector<uint32_t> commands);
  void SetUpOutputForXfer(uint32_t buffer_bytes, uint32_t period_bytes,
                          uint32_t* expected_latency_bytes);

  void TestPcmInputSetParamsAndPrepare(uint8_t channels, uint8_t format, uint8_t rate,
                                       fuchsia::media::AudioSampleFormat fidl_format,
                                       uint32_t fidl_rate, uint32_t buffer_bytes = 1024,
                                       uint32_t period_bytes = 64);
  void TestPcmInputStateTraversal(size_t capturer_id);
  void SetUpInputForXfer(uint32_t buffer_bytes, uint32_t period_bytes, uint32_t* bytes_per_frame,
                         uint32_t* bytes_per_second);

 private:
  // Using a SyncPtr can risk deadlock if a method call on this SyncPtr needs to wait for
  // an audio_service_ method to return. However, this should never happen: we only call
  // this (a) during SetUp, and (b) to NotifyQueue, which doesn't depend on audio_service_.
  fuchsia::virtualization::hardware::VirtioSoundSyncPtr sound_;
  std::array<std::unique_ptr<VirtioQueueFake>, 4> queues_;
  zx_gpaddr_t queue_data_addrs_[4];
  size_t phys_mem_size_;
  FakeAudio audio_service_;
  std::unordered_map<VirtioQueueFake*, std::unordered_set<uint32_t>> used_descriptors_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

using VirtioSoundTest = VirtioSoundTestBase<true>;
using VirtioSoundInputDisabledTest = VirtioSoundTestBase<false>;

}  // namespace

//
// GetInfo tests
//

TEST_F(VirtioSoundTest, BadRequestNoReadableDescriptors) {
  virtio_snd_hdr* resphdr;
  virtio_snd_jack_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadRequestHeaderTooSmall) {
  char query = 0;
  static_assert(1 < sizeof(virtio_snd_hdr));

  virtio_snd_hdr* resphdr;
  virtio_snd_jack_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, 1)
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, GetJackInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_JACK_INFO},
      .start_id = 0,
      .count = kNumJacks,
      .size = sizeof(virtio_snd_jack_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_jack_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());

  static_assert(kNumJacks == 1);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);
  EXPECT_EQ(resp->hdr.hda_fn_nid, 0u);
  EXPECT_EQ(resp->features, 0u);
  EXPECT_EQ(resp->hda_reg_defconf, 0x90100010u);
  EXPECT_EQ(resp->hda_reg_caps, 0x30u);
  EXPECT_EQ(resp->connected, 1u);
  for (size_t k = 0; k < sizeof(resp->padding); k++) {
    // 5.14.6.4.1.1: The device MUST initialize the padding bytes to 0
    EXPECT_EQ(resp->padding[k], 0);
  }
}

TEST_F(VirtioSoundTest, GetJackInfosFutureProof) {
  // Test that we are future-proof: correctly handle the scenario where the guest OS
  // is using a more recent version of the protocol that has a larger info struct.
  struct JackInfoV2 {
    virtio_snd_jack_info_t v1;
    uint64_t extra_field;
  };
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_JACK_INFO},
      .start_id = 0,
      .count = kNumJacks,
      .size = sizeof(JackInfoV2),
  };
  virtio_snd_hdr* resphdr;
  JackInfoV2* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());

  static_assert(kNumJacks == 1);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);
  EXPECT_EQ(resp->v1.hdr.hda_fn_nid, 0u);
  EXPECT_EQ(resp->v1.features, 0u);
  EXPECT_EQ(resp->v1.hda_reg_defconf, 0x90100010u);
  EXPECT_EQ(resp->v1.hda_reg_caps, 0x30u);
  EXPECT_EQ(resp->v1.connected, 1u);
  for (size_t k = 0; k < sizeof(resp->v1.padding); k++) {
    // 5.14.6.4.1.1: The device MUST initialize the padding bytes to 0
    EXPECT_EQ(resp->v1.padding[k], 0);
  }
  EXPECT_EQ(resp->extra_field, 0u);
}

TEST_F(VirtioSoundTest, GetPcmInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_PCM_INFO},
      .start_id = 0,
      .count = kNumStreams,
      .size = sizeof(virtio_snd_pcm_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_pcm_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumStreams * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  for (size_t k = 0; k < kNumStreams; k++) {
    SCOPED_TRACE(fxl::StringPrintf("stream %lu", k));
    uint64_t supported_formats = bit(VIRTIO_SND_PCM_FMT_U8) | bit(VIRTIO_SND_PCM_FMT_S16) |
                                 bit(VIRTIO_SND_PCM_FMT_S24) | bit(VIRTIO_SND_PCM_FMT_FLOAT);
    uint64_t supported_rates = bit(VIRTIO_SND_PCM_RATE_8000) | bit(VIRTIO_SND_PCM_RATE_11025) |
                               bit(VIRTIO_SND_PCM_RATE_16000) | bit(VIRTIO_SND_PCM_RATE_22050) |
                               bit(VIRTIO_SND_PCM_RATE_32000) | bit(VIRTIO_SND_PCM_RATE_44100) |
                               bit(VIRTIO_SND_PCM_RATE_48000) | bit(VIRTIO_SND_PCM_RATE_64000) |
                               bit(VIRTIO_SND_PCM_RATE_88200) | bit(VIRTIO_SND_PCM_RATE_96000) |
                               bit(VIRTIO_SND_PCM_RATE_176400) | bit(VIRTIO_SND_PCM_RATE_192000);

    EXPECT_EQ(resp[k].hdr.hda_fn_nid, 0u);
    EXPECT_EQ(resp[k].features, 0u);
    EXPECT_EQ(resp[k].formats, supported_formats);
    EXPECT_EQ(resp[k].rates, supported_rates);
    EXPECT_EQ(resp[k].direction, (k == 0) ? VIRTIO_SND_D_OUTPUT : VIRTIO_SND_D_INPUT);
    EXPECT_EQ(resp[k].channels_min, 1u);
    EXPECT_EQ(resp[k].channels_max, (k == kOutputStreamId) ? 2u : 1u);

    for (size_t n = 0; n < sizeof(resp[k].padding); n++) {
      // 5.14.6.6.2.1: The device MUST initialize the padding bytes to 0
      EXPECT_EQ(resp[k].padding[n], 0);
    }
  }
}

TEST_F(VirtioSoundInputDisabledTest, GetPcmInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_PCM_INFO},
      .start_id = 0,
      .count = 1,
      .size = sizeof(virtio_snd_pcm_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_pcm_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  uint64_t supported_formats = bit(VIRTIO_SND_PCM_FMT_U8) | bit(VIRTIO_SND_PCM_FMT_S16) |
                               bit(VIRTIO_SND_PCM_FMT_S24) | bit(VIRTIO_SND_PCM_FMT_FLOAT);
  uint64_t supported_rates = bit(VIRTIO_SND_PCM_RATE_8000) | bit(VIRTIO_SND_PCM_RATE_11025) |
                             bit(VIRTIO_SND_PCM_RATE_16000) | bit(VIRTIO_SND_PCM_RATE_22050) |
                             bit(VIRTIO_SND_PCM_RATE_32000) | bit(VIRTIO_SND_PCM_RATE_44100) |
                             bit(VIRTIO_SND_PCM_RATE_48000) | bit(VIRTIO_SND_PCM_RATE_64000) |
                             bit(VIRTIO_SND_PCM_RATE_88200) | bit(VIRTIO_SND_PCM_RATE_96000) |
                             bit(VIRTIO_SND_PCM_RATE_176400) | bit(VIRTIO_SND_PCM_RATE_192000);

  EXPECT_EQ(resp->hdr.hda_fn_nid, 0u);
  EXPECT_EQ(resp->features, 0u);
  EXPECT_EQ(resp->formats, supported_formats);
  EXPECT_EQ(resp->rates, supported_rates);
  EXPECT_EQ(resp->direction, VIRTIO_SND_D_OUTPUT);
  EXPECT_EQ(resp->channels_min, 1u);
  EXPECT_EQ(resp->channels_max, 2u);

  for (size_t n = 0; n < sizeof(resp->padding); n++) {
    // 5.14.6.6.2.1: The device MUST initialize the padding bytes to 0
    EXPECT_EQ(resp->padding[n], 0);
  }
}

TEST_F(VirtioSoundTest, GetChmapInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps,
      .size = sizeof(virtio_snd_chmap_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumChmaps * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  for (size_t k = 0; k < kNumChmaps; k++) {
    SCOPED_TRACE(fxl::StringPrintf("chmap %lu", k));

    EXPECT_EQ(resp[k].hdr.hda_fn_nid, 0u);
    EXPECT_EQ(resp[k].direction, (k < 2) ? VIRTIO_SND_D_OUTPUT : VIRTIO_SND_D_INPUT);
    if (k % 2 == 0) {
      // mono
      EXPECT_EQ(resp[k].channels, 1u);
      EXPECT_EQ(resp[k].positions[0], VIRTIO_SND_CHMAP_MONO);
    } else {
      // stereo
      EXPECT_EQ(resp[k].channels, 2u);
      EXPECT_EQ(resp[k].positions[0], VIRTIO_SND_CHMAP_FL);
      EXPECT_EQ(resp[k].positions[1], VIRTIO_SND_CHMAP_FR);
    }
  }
}

TEST_F(VirtioSoundTest, GetChmapInfosJustOne) {
  static_assert(kNumChmaps > 1);

  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = 1,
      .size = sizeof(virtio_snd_chmap_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, 1 * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  // chmaps[0] is OUTPUT, MONO
  EXPECT_EQ(resp[0].direction, VIRTIO_SND_D_OUTPUT);
  EXPECT_EQ(resp[0].channels, 1u);
}

TEST_F(VirtioSoundTest, GetChmapInfosSubset) {
  static_assert(kNumChmaps >= 3);

  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 1,
      .count = 2,
      .size = sizeof(virtio_snd_chmap_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, 2 * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  // chmaps[0] is OUTPUT, MONO
  // chmaps[1] is OUTPUT, STEREO
  EXPECT_EQ(resp[0].direction, VIRTIO_SND_D_OUTPUT);
  EXPECT_EQ(resp[0].channels, 2u);
  // chmaps[2] is INPUT, MONO
  EXPECT_EQ(resp[1].direction, VIRTIO_SND_D_INPUT);
  EXPECT_EQ(resp[1].channels, 1u);
}

TEST_F(VirtioSoundTest, BadGetChmapInfosRequestTooSmall) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps,
      .size = sizeof(virtio_snd_chmap_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query) - 1)  // too small
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumChmaps * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadGetChmapInfosRequestTooManyInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps + 1,  // too many
      .size = sizeof(virtio_snd_chmap_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, (kNumChmaps + 1) * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadGetChmapInfosRequestBadSizeTooSmall) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps,
      .size = sizeof(virtio_snd_chmap_info_t) - 1,  // bad size
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .AppendWritableDescriptor(&resp, kNumChmaps * sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

//
// Pcm header parsing tests
//

TEST_F(VirtioSoundTest, BadPcmRequestTooSmall) {
  virtio_snd_pcm_hdr_t query = {
      .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},  // arbitrary
      .stream_id = kOutputStreamId,
  };
  virtio_snd_hdr* resphdr;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query) - 1)  // too small
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadPcmBadStreamId) {
  virtio_snd_pcm_hdr_t query = {
      .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},  // arbitrary
      .stream_id = kOutputStreamId,
  };
  virtio_snd_hdr* resphdr;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&query, sizeof(query))
                       .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

//
// Pcm SetParameters tests
// These test that calls should succeed or fail as expected. They don't test
// AudioRenderer or AudioCapturer configurations -- that requires Prepare.
//
//

static constexpr virtio_snd_pcm_set_params_t kGoodPcmSetParams = {
    .hdr =
        {
            .hdr = {.code = VIRTIO_SND_R_PCM_SET_PARAMS},
            .stream_id = kOutputStreamId,
        },
    .buffer_bytes = 1024,
    .period_bytes = 64,
    .features = 0,
    .channels = 1,
    .format = VIRTIO_SND_PCM_FMT_FLOAT,
    .rate = VIRTIO_SND_PCM_RATE_48000,
    .padding = 0,
};

TEST_F(VirtioSoundTest, PcmSetParams) {
  auto params = kGoodPcmSetParams;
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadStreamId) {
  auto params = kGoodPcmSetParams;
  params.hdr.stream_id = kNumStreams;
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsPeriodBytesZero) {
  auto params = kGoodPcmSetParams;
  params.period_bytes = 0;  // verify we don't panic
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsPeriodBytesNotDivisorOfBufferBytes) {
  auto params = kGoodPcmSetParams;
  params.buffer_bytes = 27;
  params.period_bytes = 9;  // divides into buffer_bytes, but we have 2 bytes/frame,
  params.channels = 1;      // so there are a non-integer number of frames per period
  params.format = VIRTIO_SND_PCM_FMT_S16;
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsPeriodBytesNotMultipleOfFrameSize) {
  auto params = kGoodPcmSetParams;
  params.period_bytes = 63;  // not a divisor of buffer_bytes
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadFeatures) {
  auto params = kGoodPcmSetParams;
  params.features = (1 << VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS);  // features not supported
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadOutputChannels) {
  auto params = kGoodPcmSetParams;
  params.hdr.stream_id = kOutputStreamId;
  params.channels = 3;  // mono or stereo only
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadInputChannels) {
  auto params = kGoodPcmSetParams;
  params.hdr.stream_id = kInputStreamId;
  params.channels = 2;  // mono only
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadFormat) {
  auto params = kGoodPcmSetParams;
  params.format = 64;  // all FMT constants are < 64
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadRate) {
  auto params = kGoodPcmSetParams;
  params.rate = 64;  // all RATE constants are < 64
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundTest, BadPcmSetParamsBadPadding) {
  auto params = kGoodPcmSetParams;
  params.padding = 1;  // must be zero
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

TEST_F(VirtioSoundInputDisabledTest, BadPcmSetParamsInputDisabled) {
  auto params = kGoodPcmSetParams;
  params.hdr.stream_id = 1;  // 0=output, 1=input
  params.channels = 1;       // input is mono only
  ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_BAD_MSG, params));
}

//
// Pcm Output SetParameters+Prepare tests
// These test that AudioRenderer parameters are configured correctly.
//

template <bool EnableInput>
void VirtioSoundTestBase<EnableInput>::TestPcmOutputSetParamsAndPrepare(
    uint8_t channels, uint8_t wire_format, uint8_t wire_rate,
    fuchsia::media::AudioSampleFormat fidl_format, uint32_t fidl_rate, uint32_t buffer_bytes,
    uint32_t period_bytes) {
  {
    SCOPED_TRACE("Call SET_PARAMS");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_set_params_t{
                                             .hdr =
                                                 {
                                                     .hdr = {.code = VIRTIO_SND_R_PCM_SET_PARAMS},
                                                     .stream_id = kOutputStreamId,
                                                 },
                                             .buffer_bytes = buffer_bytes,
                                             .period_bytes = period_bytes,
                                             .features = 0,
                                             .channels = channels,
                                             .format = wire_format,
                                             .rate = wire_rate,
                                             .padding = 0,
                                         }));
  }

  {
    SCOPED_TRACE("Call PREPARE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},
                                             .stream_id = kOutputStreamId,
                                         }));
  }

  // Check that an AudioRenderer was created with the appropriate configs.
  auto renderer = get_audio_renderer(0);
  ASSERT_TRUE(renderer) << "device did not call CreateAudioRenderer?";
  auto calls = WaitForCalls(*renderer, {
                                           FakeAudioRenderer::Method::SetUsage,
                                           FakeAudioRenderer::Method::SetPcmStreamType,
                                           FakeAudioRenderer::Method::EnableMinLeadTimeEvents,
                                           FakeAudioRenderer::Method::AddPayloadBuffer,
                                       });
  ASSERT_FALSE(calls.empty());

  auto payload_buffer_size = calls[FakeAudioRenderer::Method::AddPayloadBuffer].payload_buffer_size;
  EXPECT_GE(payload_buffer_size, buffer_bytes);

  auto stream_type = calls[FakeAudioRenderer::Method::SetPcmStreamType].stream_type;
  EXPECT_EQ(stream_type.sample_format, fidl_format);
  EXPECT_EQ(stream_type.channels, static_cast<uint32_t>(channels));
  EXPECT_EQ(stream_type.frames_per_second, fidl_rate);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareUint8Mono44khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_U8, VIRTIO_SND_PCM_RATE_48000,
                                   fuchsia::media::AudioSampleFormat::UNSIGNED_8, 48000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareInt16Mono44khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000,
                                   fuchsia::media::AudioSampleFormat::SIGNED_16, 48000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareInt24Mono44khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_S24, VIRTIO_SND_PCM_RATE_48000,
                                   fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 48000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareFloatMono44khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_48000,
                                   fuchsia::media::AudioSampleFormat::FLOAT, 48000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareFloatStereo44khz) {
  TestPcmOutputSetParamsAndPrepare(2, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_48000,
                                   fuchsia::media::AudioSampleFormat::FLOAT, 48000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareFloatMono8khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_8000,
                                   fuchsia::media::AudioSampleFormat::FLOAT, 8000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareFloatMono96khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_96000,
                                   fuchsia::media::AudioSampleFormat::FLOAT, 96000);
}

TEST_F(VirtioSoundTest, PcmOutputPrepareFloatMono192khz) {
  TestPcmOutputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_192000,
                                   fuchsia::media::AudioSampleFormat::FLOAT, 192000);
}

//
// Pcm Output state tests
// These put the device through various states, including start/stop, but don't
// test the data which moves through the device, only that the state machine can
// be traversed as expected.
//
// Illegal state transitions are exercised in tests named BadPcmOutputTransition*.
//

template <bool EnableInput>
void VirtioSoundTestBase<EnableInput>::TestPcmOutputStateTraversal(size_t renderer_id) {
  {
    SCOPED_TRACE("SET_PARAMS");
    // The specific parameters don't matter except for stream_id.
    auto params = kGoodPcmSetParams;
    params.hdr.stream_id = kOutputStreamId;
    ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, params));
  }

  {
    SCOPED_TRACE("PREPARE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},
                                             .stream_id = kOutputStreamId,
                                         }));
  }

  // Check that an AudioRenderer was created with the appropriate initialization calls.
  auto renderer = get_audio_renderer(renderer_id);
  ASSERT_TRUE(renderer) << "device did not call CreateAudioRenderer?";
  auto calls = WaitForCalls(*renderer, {
                                           FakeAudioRenderer::Method::SetUsage,
                                           FakeAudioRenderer::Method::SetPcmStreamType,
                                           FakeAudioRenderer::Method::EnableMinLeadTimeEvents,
                                           FakeAudioRenderer::Method::AddPayloadBuffer,
                                       });
  ASSERT_FALSE(calls.empty());

  {
    SCOPED_TRACE("START #1");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kOutputStreamId,
                                         }));

    calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::Play});
    ASSERT_FALSE(calls.empty());
  }

  {
    SCOPED_TRACE("STOP #1");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_STOP},
                                             .stream_id = kOutputStreamId,
                                         }));
    calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::PauseNoReply,
                                     FakeAudioRenderer::Method::DiscardAllPackets});
    ASSERT_FALSE(calls.empty());
  }

  {
    SCOPED_TRACE("START #2");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kOutputStreamId,
                                         }));

    calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::Play});
    ASSERT_FALSE(calls.empty());
  }

  {
    SCOPED_TRACE("STOP #2");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_STOP},
                                             .stream_id = kOutputStreamId,
                                         }));
    calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::PauseNoReply,
                                     FakeAudioRenderer::Method::DiscardAllPackets});
    ASSERT_FALSE(calls.empty());
  }

  {
    SCOPED_TRACE("RELEASE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_RELEASE},
                                             .stream_id = kOutputStreamId,
                                         }));

    calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::Disconnect});
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, calls[FakeAudioRenderer::Method::Disconnect].disconnect_status);
  }
}

TEST_F(VirtioSoundTest, PcmOutputStateTraversal) {
  {
    SCOPED_TRACE("First traversal");
    // Run the full sequence once.
    TestPcmOutputStateTraversal(0);
  }
  {
    SCOPED_TRACE("Second traversal");
    // Running the sequence again should create a new renderer.
    TestPcmOutputStateTraversal(1);
  }
}

TEST_F(VirtioSoundTest, PcmOutputTransitionPrepareRelease) {
  {
    SCOPED_TRACE("SET_PARAMS");
    // The specific parameters don't matter except for stream_id.
    auto params = kGoodPcmSetParams;
    params.hdr.stream_id = kOutputStreamId;
    ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, params));
  }

  {
    SCOPED_TRACE("PREPARE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},
                                             .stream_id = kOutputStreamId,
                                         }));
  }

  // Check that an AudioRenderer was created with the appropriate initialization calls.
  auto renderer = get_audio_renderer(0);
  ASSERT_TRUE(renderer) << "device did not call CreateAudioRenderer?";
  auto calls = WaitForCalls(*renderer, {
                                           FakeAudioRenderer::Method::SetUsage,
                                           FakeAudioRenderer::Method::SetPcmStreamType,
                                           FakeAudioRenderer::Method::EnableMinLeadTimeEvents,
                                           FakeAudioRenderer::Method::AddPayloadBuffer,
                                       });
  ASSERT_FALSE(calls.empty());

  // Immediately release.
  {
    SCOPED_TRACE("RELEASE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_RELEASE},
                                             .stream_id = kOutputStreamId,
                                         }));

    calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::Disconnect});
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, calls[FakeAudioRenderer::Method::Disconnect].disconnect_status);
  }
}

template <bool EnableInput>
void VirtioSoundTestBase<EnableInput>::TestPcmBadTransition(uint32_t stream_id,
                                                            std::vector<uint32_t> commands) {
  {
    SCOPED_TRACE("SET_PARAMS");
    // The specific parameters don't matter except for stream_id.
    auto params = kGoodPcmSetParams;
    params.hdr.stream_id = stream_id;
    ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, params));
  }

  // All commands except the last should succeed.
  ASSERT_GT(commands.size(), 0u);
  for (size_t k = 0; k < commands.size() - 1; k++) {
    SCOPED_TRACE(fxl::StringPrintf("CODE=%u", commands[k]));
    ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                                                 .hdr = {.code = commands[k]},
                                                                 .stream_id = stream_id,
                                                             }));
  }

  // The last command should fail.
  {
    virtio_snd_pcm_hdr_t msg{
        .hdr = {.code = commands.back()},
        .stream_id = stream_id,
    };
    virtio_snd_hdr* resphdr;
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                         .AppendReadableDescriptor(&msg, sizeof(msg))
                         .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                         .Build());
    ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));
    ASSERT_EQ(ZX_OK, WaitOnInterrupt());
    EXPECT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
  }
}

TEST_F(VirtioSoundTest, BadPcmOutputTransitionPrepareStop) {
  // Can't transition from PREPARE to STOP.
  TestPcmBadTransition(kOutputStreamId, {VIRTIO_SND_R_PCM_PREPARE, VIRTIO_SND_R_PCM_STOP});
}

TEST_F(VirtioSoundTest, BadPcmOutputTransitionReleaseStart) {
  // Can't transition from RELEASE to START.
  TestPcmBadTransition(kOutputStreamId, {VIRTIO_SND_R_PCM_PREPARE, VIRTIO_SND_R_PCM_RELEASE,
                                         VIRTIO_SND_R_PCM_START});
}

TEST_F(VirtioSoundTest, BadPcmOutputTransitionReleaseStop) {
  // Can't transition from RELEASE to STOP.
  TestPcmBadTransition(kOutputStreamId,
                       {VIRTIO_SND_R_PCM_PREPARE, VIRTIO_SND_R_PCM_RELEASE, VIRTIO_SND_R_PCM_STOP});
}

//
// Pcm Output data tests
//

template <bool EnableInput>
void VirtioSoundTestBase<EnableInput>::SetUpOutputForXfer(uint32_t buffer_bytes,
                                                          uint32_t period_bytes,
                                                          uint32_t* expected_latency_bytes) {
  // 2 bytes/frame, so the caller must set period_bytes to a multiple of 2.
  const auto bytes_per_frame = 2;
  ASSERT_TRUE(period_bytes % bytes_per_frame == 0);

  // U8 with 2 channels is 2 bytes/frame.
  const auto channels = 2;
  const auto fidl_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
  const auto fidl_fps = 48000;
  ASSERT_NO_FATAL_FAILURE(TestPcmOutputSetParamsAndPrepare(channels, VIRTIO_SND_PCM_FMT_U8,
                                                           VIRTIO_SND_PCM_RATE_48000,
                                                           // The FIDL equivalents.
                                                           fidl_format, fidl_fps,
                                                           // Caller's params.
                                                           buffer_bytes, period_bytes));

  auto lead_time = FakeAudioRenderer::kDefaultMinLeadTime + kDeadlinePeriod;
  *expected_latency_bytes =
      static_cast<uint32_t>(lead_time.to_nsecs() * fidl_fps * bytes_per_frame / 1'000'000'000);
}

TEST_F(VirtioSoundTest, PcmOutputXferOne) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t expected_latency_bytes;
  ASSERT_NO_FATAL_FAILURE(SetUpOutputForXfer(kBufferBytes, kPeriodBytes, &expected_latency_bytes));

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  // Send a packet.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kOutputStreamId};
  virtio_snd_pcm_status_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
  resp->status = kInvalidStatus;

  // Wait for the packet to arrive.
  auto renderer = get_audio_renderer(0);
  auto packets = WaitForPackets(*renderer, 1);
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(packets[0].buffer, kPacket);
  EXPECT_EQ(resp->status, kInvalidStatus);

  // Wait for the reply.
  packets[0].callback();
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  EXPECT_EQ(resp->status, VIRTIO_SND_S_OK);
  EXPECT_EQ(resp->latency_bytes, expected_latency_bytes);
}

TEST_F(VirtioSoundTest, PcmOutputXferMultiple) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t expected_latency_bytes;
  ASSERT_NO_FATAL_FAILURE(SetUpOutputForXfer(kBufferBytes, kPeriodBytes, &expected_latency_bytes));

  // Send multiple packets.
  constexpr size_t kNumPackets = 3;
  const std::string kPackets[kNumPackets] = {"aaaaaaaa", "bbbbbbbb", "cccccccc"};
  virtio_snd_pcm_xfer_t xfer{.stream_id = kOutputStreamId};
  virtio_snd_pcm_status_t* resp[kNumPackets];
  for (size_t k = 0; k < kNumPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    const uint32_t size = static_cast<uint32_t>(kPackets[k].size());
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendReadableDescriptor(&kPackets[k][0], size)
                         .AppendWritableDescriptor(&resp[k], sizeof(resp[k]))
                         .Build());
    ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
    resp[k]->status = kInvalidStatus;
  }

  // Wait for the packets to arrive.
  auto renderer = get_audio_renderer(0);
  auto packets = WaitForPackets(*renderer, 3);
  ASSERT_EQ(packets.size(), kNumPackets);
  for (size_t k = 0; k < kNumPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    EXPECT_EQ(packets[k].buffer, kPackets[k]);
    EXPECT_EQ(resp[k]->status, kInvalidStatus);
  }

  // Wait for the replies.
  for (size_t k = 0; k < kNumPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    packets[k].callback();
    ASSERT_EQ(ZX_OK, WaitOnInterrupt());
    EXPECT_EQ(resp[k]->status, VIRTIO_SND_S_OK);
    EXPECT_EQ(resp[k]->latency_bytes, expected_latency_bytes);
  }
}

// TODO(fxbug.dev/99083): Re-enable this test once the flake is fixed.
TEST_F(VirtioSoundTest, DISABLED_PcmOutputXferThenRelease) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t expected_latency_bytes;
  ASSERT_NO_FATAL_FAILURE(SetUpOutputForXfer(kBufferBytes, kPeriodBytes, &expected_latency_bytes));

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  constexpr size_t kPacketsBeforeRelease = 2;
  constexpr size_t kPacketsDuringRelease = 2;
  constexpr size_t kPacketsAfterRelease = 2;
  constexpr size_t kTotalPackets =
      kPacketsBeforeRelease + kPacketsDuringRelease + kPacketsAfterRelease;
  virtio_snd_pcm_xfer_t xfer{.stream_id = kOutputStreamId};
  virtio_snd_pcm_status_t* xfer_resp[kTotalPackets];

  // Send multiple packets.
  uint16_t xfer_index[kTotalPackets];
  for (size_t k = 0; k < kPacketsBeforeRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                         .AppendWritableDescriptor(&xfer_resp[k], sizeof(xfer_resp[k]))
                         .Build(&xfer_index[k]));
    ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
    xfer_resp[k]->status = kInvalidStatus;
  }

  // Wait for those packets to arrive.
  auto renderer = get_audio_renderer(0);
  auto packets = WaitForPackets(*renderer, kPacketsBeforeRelease);
  ASSERT_EQ(packets.size(), kPacketsBeforeRelease);
  for (size_t k = 0; k < kPacketsBeforeRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    EXPECT_EQ(packets[k].buffer, kPacket);
    EXPECT_EQ(xfer_resp[k]->status, kInvalidStatus);
  }

  // Send a RELEASE command.
  virtio_snd_hdr* release_resp;
  virtio_snd_pcm_hdr_t release_msg{
      .hdr = {.code = VIRTIO_SND_R_PCM_RELEASE},
      .stream_id = kOutputStreamId,
  };
  uint16_t release_index;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&release_msg, sizeof(release_msg))
                       .AppendWritableDescriptor(&release_resp, sizeof(*release_resp))
                       .Build(&release_index));
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));

  // Send more packets while the RELEASE is in flight.
  // Since these are sent on a different queue, they may arrive before RELEASE.
  // This tests how we handle packets that arrive concurrently with RELEASE.
  for (size_t k = kPacketsBeforeRelease; k < kPacketsBeforeRelease + kPacketsDuringRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                         .AppendWritableDescriptor(&xfer_resp[k], sizeof(xfer_resp[k]))
                         .Build(&xfer_index[k]));
    ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
    xfer_resp[k]->status = kInvalidStatus;
  }

  // All packets we've sent so far should compilete automatically.
  // Packets sent before RELEASE should completed with IO_ERR, while packets sent
  // concurrently with RELEASE may be complete with either IO_ERR or BAD_MSG,
  // depending on how the packet is ordered relative to the RELEASE.
  for (size_t k = 0; k < kPacketsBeforeRelease + kPacketsDuringRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, WaitForDescriptor(txq(), xfer_index[k]));
    if (k < kPacketsBeforeRelease) {
      EXPECT_EQ(xfer_resp[k]->status, VIRTIO_SND_S_IO_ERR);
    } else {
      using ::testing::AnyOf;
      using ::testing::Eq;
      EXPECT_THAT(xfer_resp[k]->status, AnyOf(Eq(VIRTIO_SND_S_BAD_MSG), Eq(VIRTIO_SND_S_IO_ERR)));
    }
    EXPECT_EQ(xfer_resp[k]->latency_bytes, 0u);
  }

  // Wait for the REELASE.
  ASSERT_EQ(ZX_OK, WaitForDescriptor(controlq(), release_index));
  ASSERT_EQ(release_resp->code, VIRTIO_SND_S_OK);
  auto calls = WaitForCalls(*renderer, {FakeAudioRenderer::Method::Disconnect});
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, calls[FakeAudioRenderer::Method::Disconnect].disconnect_status);

  // Send more packets. Each should fail immediately because we have disconnected.
  for (size_t k = kPacketsBeforeRelease + kPacketsDuringRelease; k < kTotalPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                         .AppendWritableDescriptor(&xfer_resp[k], sizeof(xfer_resp[k]))
                         .Build(&xfer_index[k]));
    xfer_resp[k]->status = kInvalidStatus;
    ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
    ASSERT_EQ(ZX_OK, WaitForDescriptor(txq(), xfer_index[k]));
    EXPECT_EQ(xfer_resp[k]->status, VIRTIO_SND_S_BAD_MSG);
    EXPECT_EQ(xfer_resp[k]->latency_bytes, 0u);
  }
}

TEST_F(VirtioSoundTest, BadPcmOutputXferBadStreamId) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t expected_latency_bytes;
  ASSERT_NO_FATAL_FAILURE(SetUpOutputForXfer(kBufferBytes, kPeriodBytes, &expected_latency_bytes));

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  // Send a packet.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kNumStreams};
  virtio_snd_pcm_status_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  auto renderer = get_audio_renderer(0);
  ASSERT_TRUE(renderer);
  EXPECT_EQ(renderer->packets().size(), 0u);
  EXPECT_EQ(resp->status, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadPcmOutputXferPacketTooBig) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t expected_latency_bytes;
  ASSERT_NO_FATAL_FAILURE(SetUpOutputForXfer(kBufferBytes, kPeriodBytes, &expected_latency_bytes));

  const std::string kPacket = "1234567890";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());
  ASSERT_GT(kPacketSize, kPeriodBytes);

  // Send a packet.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kOutputStreamId};
  virtio_snd_pcm_status_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  auto renderer = get_audio_renderer(0);
  ASSERT_TRUE(renderer);
  EXPECT_EQ(renderer->packets().size(), 0u);
  EXPECT_EQ(resp->status, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadPcmOutputXferPacketNonintegralFrames) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t expected_latency_bytes;
  ASSERT_NO_FATAL_FAILURE(SetUpOutputForXfer(kBufferBytes, kPeriodBytes, &expected_latency_bytes));

  const std::string kPacket = "1234567";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());
  ASSERT_LT(kPacketSize, kPeriodBytes);
  ASSERT_EQ(kPacketSize % 2, 1u);

  // Send a packet.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kNumStreams};
  virtio_snd_pcm_status_t* resp;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(txq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendReadableDescriptor(&kPacket[0], kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(TXQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  auto renderer = get_audio_renderer(0);
  ASSERT_TRUE(renderer);
  EXPECT_EQ(renderer->packets().size(), 0u);
  EXPECT_EQ(resp->status, VIRTIO_SND_S_BAD_MSG);
}

//
// Pcm Input SetParameters+Prepare tests
// These test that AudioCapturer parameters are configured correctly.
//

template <>
void VirtioSoundTestBase<true>::TestPcmInputSetParamsAndPrepare(
    uint8_t channels, uint8_t wire_format, uint8_t wire_rate,
    fuchsia::media::AudioSampleFormat fidl_format, uint32_t fidl_rate, uint32_t buffer_bytes,
    uint32_t period_bytes) {
  {
    SCOPED_TRACE("Call SET_PARAMS");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_set_params_t{
                                             .hdr =
                                                 {
                                                     .hdr = {.code = VIRTIO_SND_R_PCM_SET_PARAMS},
                                                     .stream_id = kInputStreamId,
                                                 },
                                             .buffer_bytes = buffer_bytes,
                                             .period_bytes = period_bytes,
                                             .features = 0,
                                             .channels = channels,
                                             .format = wire_format,
                                             .rate = wire_rate,
                                             .padding = 0,
                                         }));
  }

  {
    SCOPED_TRACE("Call PREPARE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  // Check that an AudioCapturer was created with the appropriate configs.
  auto capturer = get_audio_capturer(0);
  ASSERT_TRUE(capturer) << "device did not call CreateAudioCapturer?";
  auto calls = WaitForCalls(*capturer, {
                                           FakeAudioCapturer::Method::SetUsage,
                                           FakeAudioCapturer::Method::SetPcmStreamType,
                                           FakeAudioCapturer::Method::AddPayloadBuffer,
                                       });
  ASSERT_FALSE(calls.empty());

  auto payload_buffer_size = calls[FakeAudioCapturer::Method::AddPayloadBuffer].payload_buffer_size;
  EXPECT_GE(payload_buffer_size, buffer_bytes);

  auto stream_type = calls[FakeAudioCapturer::Method::SetPcmStreamType].stream_type;
  EXPECT_EQ(stream_type.sample_format, fidl_format);
  EXPECT_EQ(stream_type.channels, static_cast<uint32_t>(channels));
  EXPECT_EQ(stream_type.frames_per_second, fidl_rate);
}

TEST_F(VirtioSoundTest, PcmInputPrepareUint8Mono44khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_U8, VIRTIO_SND_PCM_RATE_48000,
                                  fuchsia::media::AudioSampleFormat::UNSIGNED_8, 48000);
}

TEST_F(VirtioSoundTest, PcmInputPrepareInt16Mono44khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000,
                                  fuchsia::media::AudioSampleFormat::SIGNED_16, 48000);
}

TEST_F(VirtioSoundTest, PcmInputPrepareInt24Mono44khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_S24, VIRTIO_SND_PCM_RATE_48000,
                                  fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 48000);
}

TEST_F(VirtioSoundTest, PcmInputPrepareFloatMono44khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_48000,
                                  fuchsia::media::AudioSampleFormat::FLOAT, 48000);
}

TEST_F(VirtioSoundTest, PcmInputPrepareFloatMono8khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_8000,
                                  fuchsia::media::AudioSampleFormat::FLOAT, 8000);
}

TEST_F(VirtioSoundTest, PcmInputPrepareFloatMono96khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_96000,
                                  fuchsia::media::AudioSampleFormat::FLOAT, 96000);
}

TEST_F(VirtioSoundTest, PcmInputPrepareFloatMono192khz) {
  TestPcmInputSetParamsAndPrepare(1, VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_192000,
                                  fuchsia::media::AudioSampleFormat::FLOAT, 192000);
}

//
// Pcm Input state tests
// These put the device through various states, including start/stop, but don't
// test the data which moves through the device, only that the state machine can
// be traversed as expected.
//
// Illegal state transitions are exercised in tests named BadPcmInputTransition*.
//

template <>
void VirtioSoundTestBase<true>::TestPcmInputStateTraversal(size_t capturer_id) {
  {
    SCOPED_TRACE("SET_PARAMS");
    // The specific parameters don't matter except for stream_id.
    auto params = kGoodPcmSetParams;
    params.hdr.stream_id = kInputStreamId;
    ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, params));
  }

  {
    SCOPED_TRACE("PREPARE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  // Check that an AudioCapturer was created with the appropriate initialization calls.
  auto capturer = get_audio_capturer(capturer_id);
  ASSERT_TRUE(capturer) << "device did not call CreateAudioCapturer?";
  auto calls = WaitForCalls(*capturer, {
                                           FakeAudioCapturer::Method::SetUsage,
                                           FakeAudioCapturer::Method::SetPcmStreamType,
                                           FakeAudioCapturer::Method::AddPayloadBuffer,
                                       });
  ASSERT_FALSE(calls.empty());

  // START and STOP don't directly make calls to the AudioCapturer since we don't
  // have any RX packets queued.
  {
    SCOPED_TRACE("START #1");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  {
    SCOPED_TRACE("STOP #1");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_STOP},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  {
    SCOPED_TRACE("START #2");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  {
    SCOPED_TRACE("STOP #2");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_STOP},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  {
    SCOPED_TRACE("RELEASE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_RELEASE},
                                             .stream_id = kInputStreamId,
                                         }));

    calls = WaitForCalls(*capturer, {FakeAudioCapturer::Method::Disconnect});
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, calls[FakeAudioCapturer::Method::Disconnect].disconnect_status);
  }
}

TEST_F(VirtioSoundTest, PcmInputStateTraversal) {
  {
    SCOPED_TRACE("First traversal");
    // Run the full sequence once.
    TestPcmInputStateTraversal(0);
  }
  {
    SCOPED_TRACE("Second traversal");
    // Running the sequence again should create a new capturer.
    TestPcmInputStateTraversal(1);
  }
}

TEST_F(VirtioSoundTest, PcmInputTransitionPrepareRelease) {
  {
    SCOPED_TRACE("SET_PARAMS");
    // The specific parameters don't matter except for stream_id.
    auto params = kGoodPcmSetParams;
    params.hdr.stream_id = kInputStreamId;
    ASSERT_NO_FATAL_FAILURE(CheckSimpleCall(VIRTIO_SND_S_OK, params));
  }

  {
    SCOPED_TRACE("PREPARE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_PREPARE},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  // Check that an AudioCapturer was created with the appropriate initialization calls.
  auto capturer = get_audio_capturer(0);
  ASSERT_TRUE(capturer) << "device did not call CreateAudioCapturer?";
  auto calls = WaitForCalls(*capturer, {
                                           FakeAudioCapturer::Method::SetUsage,
                                           FakeAudioCapturer::Method::SetPcmStreamType,
                                           FakeAudioCapturer::Method::AddPayloadBuffer,
                                       });
  ASSERT_FALSE(calls.empty());

  // Immediately release.
  {
    SCOPED_TRACE("RELEASE");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_RELEASE},
                                             .stream_id = kInputStreamId,
                                         }));

    calls = WaitForCalls(*capturer, {FakeAudioCapturer::Method::Disconnect});
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, calls[FakeAudioCapturer::Method::Disconnect].disconnect_status);
  }
}

TEST_F(VirtioSoundTest, BadPcmInputTransitionPrepareStop) {
  // Can't transition from PREPARE to STOP.
  TestPcmBadTransition(kInputStreamId, {VIRTIO_SND_R_PCM_PREPARE, VIRTIO_SND_R_PCM_STOP});
}

TEST_F(VirtioSoundTest, BadPcmInputTransitionReleaseStart) {
  // Can't transition from RELEASE to START.
  TestPcmBadTransition(
      kInputStreamId, {VIRTIO_SND_R_PCM_PREPARE, VIRTIO_SND_R_PCM_RELEASE, VIRTIO_SND_R_PCM_START});
}

TEST_F(VirtioSoundTest, BadPcmInputTransitionReleaseStop) {
  // Can't transition from RELEASE to STOP.
  TestPcmBadTransition(kInputStreamId,
                       {VIRTIO_SND_R_PCM_PREPARE, VIRTIO_SND_R_PCM_RELEASE, VIRTIO_SND_R_PCM_STOP});
}

//
// Pcm Input data tests
//

template <>
void VirtioSoundTestBase<true>::SetUpInputForXfer(uint32_t buffer_bytes, uint32_t period_bytes,
                                                  uint32_t* bytes_per_frame,
                                                  uint32_t* bytes_per_second) {
  // 2 bytes/frame, so the caller must set period_bytes to a multiple of 2.
  *bytes_per_frame = 2;
  ASSERT_TRUE(period_bytes % *bytes_per_frame == 0);

  // U16 with 1 channels is 2 bytes/frame.
  const auto channels = 1;
  const auto fidl_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  const auto fidl_fps = 48000;
  ASSERT_NO_FATAL_FAILURE(TestPcmInputSetParamsAndPrepare(channels, VIRTIO_SND_PCM_FMT_S16,
                                                          VIRTIO_SND_PCM_RATE_48000,
                                                          // The FIDL equivalents.
                                                          fidl_format, fidl_fps,
                                                          // Caller's params.
                                                          buffer_bytes, period_bytes));

  *bytes_per_second = *bytes_per_frame * fidl_fps;
}

TEST_F(VirtioSoundTest, PcmInputXferOne) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  // Send a packet.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kInputStreamId};
  virtio_snd_pcm_status_t* resp;
  char* resp_data;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendWritableDescriptor(&resp_data, kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
  resp->status = kInvalidStatus;

  // The packet should not arrive until after we START.
  RunLoopUntilIdle();
  auto capturer = get_audio_capturer(0);
  ASSERT_EQ(capturer->packets().size(), 0u);

  {
    SCOPED_TRACE("START");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  auto packets = WaitForPackets(*capturer, 1);
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(resp->status, kInvalidStatus);

  // Release the packet and wait for the RXQ reply.
  capturer->ReleasePacket(packets[0], kPacket, bytes_per_frame, zx::sec(1));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  EXPECT_EQ(resp->status, VIRTIO_SND_S_OK);
  // Since we sent a packet with a PTS 1s in the past, the latency should be at least 1s.
  // The latency measured by our device may be larger due scheduling delays.
  EXPECT_GE(resp->latency_bytes, bytes_per_second);
  EXPECT_EQ(std::string(resp_data, kPacketSize), kPacket);
}

TEST_F(VirtioSoundTest, PcmInputXferOneAfterStart) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  {
    SCOPED_TRACE("START");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  // Send a packet and wait for it to be released.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kInputStreamId};
  virtio_snd_pcm_status_t* resp;
  char* resp_data;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendWritableDescriptor(&resp_data, kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
  resp->status = kInvalidStatus;

  auto capturer = get_audio_capturer(0);
  auto packets = WaitForPackets(*capturer, 1);
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(resp->status, kInvalidStatus);

  // Release the packet and wait for the RXQ reply.
  capturer->ReleasePacket(packets[0], kPacket, bytes_per_frame, zx::sec(1));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  EXPECT_EQ(resp->status, VIRTIO_SND_S_OK);
  // Since we sent a packet with a PTS 1s in the past, the latency should be at least 1s.
  // The latency measured by our device may be larger due scheduling delays.
  EXPECT_GE(resp->latency_bytes, bytes_per_second);
  EXPECT_EQ(std::string(resp_data, kPacketSize), kPacket);
}

TEST_F(VirtioSoundTest, PcmInputXferMultiple) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  // Send multiple packets.
  constexpr size_t kNumPackets = 3;
  const std::string kPackets[kNumPackets] = {"aaaaaaaa", "bbbbbbbb", "cccccccc"};
  virtio_snd_pcm_xfer_t xfer{.stream_id = kInputStreamId};
  virtio_snd_pcm_status_t* resp[kNumPackets];
  char* resp_data[kNumPackets];
  for (size_t k = 0; k < kNumPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    const uint32_t size = static_cast<uint32_t>(kPackets[k].size());
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendWritableDescriptor(&resp_data[k], size)
                         .AppendWritableDescriptor(&resp[k], sizeof(*resp))
                         .Build());
    ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
    resp[k]->status = kInvalidStatus;
  }

  // The packet should not arrive until after we START.
  RunLoopUntilIdle();
  auto capturer = get_audio_capturer(0);
  ASSERT_EQ(capturer->packets().size(), 0u);

  {
    SCOPED_TRACE("START");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  auto packets = WaitForPackets(*capturer, kNumPackets);
  ASSERT_EQ(packets.size(), kNumPackets);
  for (size_t k = 0; k < kNumPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    EXPECT_EQ(resp[k]->status, kInvalidStatus);
  }

  // Release the packets and wait for the RXQ replies.
  for (size_t k = 0; k < kNumPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    capturer->ReleasePacket(packets[k], kPackets[k], bytes_per_frame, zx::sec(1));
    ASSERT_EQ(ZX_OK, WaitOnInterrupt());
    EXPECT_EQ(resp[k]->status, VIRTIO_SND_S_OK);
    // Since we sent a packet with a PTS 1s in the past, the latency should be at least 1s.
    // The latency measured by our device may be larger due scheduling delays.
    EXPECT_GE(resp[k]->latency_bytes, bytes_per_second);
    EXPECT_EQ(std::string(resp_data[k], kPackets[k].size()), kPackets[k]);
  }
}

// TODO(fxbug.dev/99083): Re-enable this test once the flake is fixed.
TEST_F(VirtioSoundTest, DISABLED_PcmInputXferThenRelease) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  {
    SCOPED_TRACE("START");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_START},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  constexpr size_t kPacketsBeforeRelease = 2;
  constexpr size_t kPacketsDuringRelease = 2;
  constexpr size_t kPacketsAfterRelease = 2;
  constexpr size_t kTotalPackets =
      kPacketsBeforeRelease + kPacketsDuringRelease + kPacketsAfterRelease;
  virtio_snd_pcm_xfer_t xfer{.stream_id = kInputStreamId};
  virtio_snd_pcm_status_t* xfer_resp[kTotalPackets];
  char* xfer_data[kTotalPackets];
  uint16_t xfer_index[kTotalPackets];

  // Send multiple packets.
  for (size_t k = 0; k < kPacketsBeforeRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendWritableDescriptor(&xfer_data[k], kPacketSize)
                         .AppendWritableDescriptor(&xfer_resp[k], sizeof(xfer_resp[k]))
                         .Build(&xfer_index[k]));
    ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
    xfer_resp[k]->status = kInvalidStatus;
  }

  // Wait for those packets to arrive.
  auto capturer = get_audio_capturer(0);
  auto packets = WaitForPackets(*capturer, kPacketsBeforeRelease);
  ASSERT_EQ(packets.size(), kPacketsBeforeRelease);
  for (size_t k = 0; k < kPacketsBeforeRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    EXPECT_EQ(xfer_resp[k]->status, kInvalidStatus);
  }

  // RELEASE must be called while stopped.
  {
    SCOPED_TRACE("STOP");
    ASSERT_NO_FATAL_FAILURE(
        CheckSimpleCall(VIRTIO_SND_S_OK, virtio_snd_pcm_hdr_t{
                                             .hdr = {.code = VIRTIO_SND_R_PCM_STOP},
                                             .stream_id = kInputStreamId,
                                         }));
  }

  // Send a RELEASE command.
  virtio_snd_hdr* release_resp;
  virtio_snd_pcm_hdr_t release_msg{
      .hdr = {.code = VIRTIO_SND_R_PCM_RELEASE},
      .stream_id = kInputStreamId,
  };
  uint16_t release_index;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(controlq())
                       .AppendReadableDescriptor(&release_msg, sizeof(release_msg))
                       .AppendWritableDescriptor(&release_resp, sizeof(*release_resp))
                       .Build(&release_index));
  ASSERT_EQ(ZX_OK, NotifyQueue(CONTROLQ));

  // Send more packets while the RELEASE is in flight.
  // Since these are sent on a different queue, they may arrive before RELEASE.
  // This tests how we handle packets that arrive concurrently with RELEASE.
  for (size_t k = kPacketsBeforeRelease; k < kPacketsBeforeRelease + kPacketsDuringRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendWritableDescriptor(&xfer_data[k], kPacketSize)
                         .AppendWritableDescriptor(&xfer_resp[k], sizeof(xfer_resp[k]))
                         .Build(&xfer_index[k]));
    ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
    xfer_resp[k]->status = kInvalidStatus;
  }

  // All packets we've sent so far should compilete automatically.
  // Packets sent before RELEASE should completed with IO_ERR, while packets sent
  // concurrently with RELEASE may be complete with either IO_ERR or BAD_MSG,
  // depending on how the packet is ordered relative to the RELEASE.
  for (size_t k = 0; k < kPacketsBeforeRelease + kPacketsDuringRelease; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, WaitForDescriptor(rxq(), xfer_index[k]));
    if (k < kPacketsBeforeRelease) {
      EXPECT_EQ(xfer_resp[k]->status, VIRTIO_SND_S_IO_ERR);
    } else {
      using ::testing::AnyOf;
      using ::testing::Eq;
      EXPECT_THAT(xfer_resp[k]->status, AnyOf(Eq(VIRTIO_SND_S_BAD_MSG), Eq(VIRTIO_SND_S_IO_ERR)));
    }
    EXPECT_EQ(xfer_resp[k]->latency_bytes, 0u);
  }

  // Wait for the REELASE.
  ASSERT_EQ(ZX_OK, WaitForDescriptor(controlq(), release_index));
  ASSERT_EQ(release_resp->code, VIRTIO_SND_S_OK);
  auto calls = WaitForCalls(*capturer, {FakeAudioCapturer::Method::Disconnect});
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, calls[FakeAudioCapturer::Method::Disconnect].disconnect_status);

  // Send more packets. Each should fail immediately because we have disconnected.
  for (size_t k = kPacketsBeforeRelease + kPacketsDuringRelease; k < kTotalPackets; k++) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", k));
    ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                         .AppendReadableDescriptor(&xfer, sizeof(xfer))
                         .AppendWritableDescriptor(&xfer_data[k], kPacketSize)
                         .AppendWritableDescriptor(&xfer_resp[k], sizeof(xfer_resp[k]))
                         .Build(&xfer_index[k]));
    xfer_resp[k]->status = kInvalidStatus;
    ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
    ASSERT_EQ(ZX_OK, WaitForDescriptor(rxq(), xfer_index[k]));
    EXPECT_EQ(xfer_resp[k]->status, VIRTIO_SND_S_BAD_MSG);
    EXPECT_EQ(xfer_resp[k]->latency_bytes, 0u);
  }
}

TEST_F(VirtioSoundTest, BadPcmInputXferBadStreamId) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  const std::string kPacket = "12345678";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());

  // Packet has an invalid stream ID.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kNumStreams};
  virtio_snd_pcm_status_t* resp;
  char* resp_data;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendWritableDescriptor(&resp_data, kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  auto capturer = get_audio_capturer(0);
  ASSERT_TRUE(capturer);
  EXPECT_EQ(capturer->packets().size(), 0u);
  EXPECT_EQ(resp->status, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadPcmInputXferPacketTooBig) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  const std::string kPacket = "1234567890";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());
  ASSERT_GT(kPacketSize, kPeriodBytes);

  // Packet buffer size is too big.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kOutputStreamId};
  virtio_snd_pcm_status_t* resp;
  char* resp_data;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendWritableDescriptor(&resp_data, kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  auto capturer = get_audio_capturer(0);
  ASSERT_TRUE(capturer);
  EXPECT_EQ(capturer->packets().size(), 0u);
  EXPECT_EQ(resp->status, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadPcmInputXferPacketNonintegralFrames) {
  constexpr uint32_t kBufferBytes = 64;
  constexpr uint32_t kPeriodBytes = 8;
  uint32_t bytes_per_frame;
  uint32_t bytes_per_second;
  ASSERT_NO_FATAL_FAILURE(
      SetUpInputForXfer(kBufferBytes, kPeriodBytes, &bytes_per_frame, &bytes_per_second));

  const std::string kPacket = "1234567";
  const uint32_t kPacketSize = static_cast<uint32_t>(kPacket.size());
  ASSERT_LT(kPacketSize, kPeriodBytes);
  ASSERT_EQ(kPacketSize % 2, 1u);

  // Packet buffer size is too big.
  virtio_snd_pcm_xfer_t xfer{.stream_id = kOutputStreamId};
  virtio_snd_pcm_status_t* resp;
  char* resp_data;
  ASSERT_EQ(ZX_OK, DescriptorChainBuilder(rxq())
                       .AppendReadableDescriptor(&xfer, sizeof(xfer))
                       .AppendWritableDescriptor(&resp_data, kPacketSize)
                       .AppendWritableDescriptor(&resp, sizeof(*resp))
                       .Build());
  ASSERT_EQ(ZX_OK, NotifyQueue(RXQ));
  ASSERT_EQ(ZX_OK, WaitOnInterrupt());
  auto capturer = get_audio_capturer(0);
  ASSERT_TRUE(capturer);
  EXPECT_EQ(capturer->packets().size(), 0u);
  EXPECT_EQ(resp->status, VIRTIO_SND_S_BAD_MSG);
}
