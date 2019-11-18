// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/clock.h>
#include <math.h>
#include <zircon/time.h>

#include <memory>

#include "fuchsia/media/cpp/fidl.h"
#include "gtest/gtest.h"
#include "lib/async/cpp/task.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fit/function.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/media/cpp/timeline_rate.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/playback/mediaplayer/graph/graph.h"
#include "src/media/playback/mediaplayer/graph/types/audio_stream_type.h"
#include "src/media/playback/mediaplayer/process/processor.h"

namespace media_player {
namespace test {

static constexpr uint64_t kBufferSize = 1024;
static constexpr int64_t kNoPts = std::numeric_limits<int64_t>::max();
static constexpr uint64_t kDefaultMinLeadTime = ZX_MSEC(10);
static constexpr uint64_t kTargetLeadTime = kDefaultMinLeadTime + ZX_MSEC(10);

// keep in sync with value in fidl_audio_renderer.cc
static constexpr int64_t kNoPtsSlipOnStarveNs = ZX_MSEC(500);

// Fake implementation of |fuchsia::media::StreamProcessor|.
class FakeAudioRenderer : public fuchsia::media::AudioRenderer {
 public:
  using PacketHandler_ = fit::callback<void(fuchsia::media::StreamPacket)>;

  FakeAudioRenderer() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request) {
    binding_.Bind(std::move(request));
  }

  void SetPacketHandler(PacketHandler_ callback) { packet_handler_callback_ = std::move(callback); }

  // fuchsia::media::AudioRenderer implementation.
  void AddPayloadBuffer(uint32_t id, ::zx::vmo payload_buffer) {}
  void RemovePayloadBuffer(uint32_t id) {}
  void SendPacket(fuchsia::media::StreamPacket packet, SendPacketCallback callback) {
    if (packet_handler_callback_) {
      packet_handler_callback_(packet);
    }
    callback();
  }
  void SendPacketNoReply(fuchsia::media::StreamPacket packet) {}
  void EndOfStream() {}
  void DiscardAllPackets(DiscardAllPacketsCallback callback) {}
  void DiscardAllPacketsNoReply() {}
  void SetPcmStreamType(fuchsia::media::AudioStreamType type) {}
  void SetPtsUnits(uint32_t tick_per_second_numerator, uint32_t tick_per_second_denominator) {}
  void SetPtsContinuityThreshold(float threshold_seconds) {}
  void SetReferenceClock(::zx::handle reference_clock) {}
  void Play(int64_t reference_time, int64_t media_time, PlayCallback callback) {}
  void PlayNoReply(int64_t reference_time, int64_t media_time) {}
  void Pause(PauseCallback callback) {}
  void PauseNoReply() {}
  void EnableMinLeadTimeEvents(bool enabled) {
    binding_.events().OnMinLeadTimeChanged(kDefaultMinLeadTime);
  }
  void GetMinLeadTime(GetMinLeadTimeCallback callback) {}
  void BindGainControl(
      ::fidl::InterfaceRequest<::fuchsia::media::audio::GainControl> gain_control_request) {}
  void SetUsage(fuchsia::media::AudioRenderUsage usage) {}

 private:
  fidl::Binding<fuchsia::media::AudioRenderer> binding_;
  PacketHandler_ packet_handler_callback_;
};

class FakeAudioDecoder : public Processor {
 public:
  static std::unique_ptr<StreamType> OutputStreamType(const StreamType& stream_type) {
    return AudioStreamType::Create(
        nullptr, StreamType::kAudioEncodingLpcm, nullptr, stream_type.audio()->sample_format(),
        stream_type.audio()->channels(), stream_type.audio()->frames_per_second());
  }

  explicit FakeAudioDecoder(const StreamType& stream_type)
      : output_stream_type_(OutputStreamType(stream_type)) {}

  ~FakeAudioDecoder() override = default;

  const char* label() const override { return "FakeAudioDecoder"; }

  // Processor implementation.
  void ConfigureConnectors() override {
    ConfigureInputToUseLocalMemory(1,    // max_aggregate_payload_size
                                   0);   // max_payload_count
    ConfigureOutputToUseLocalMemory(1,   // max_aggregate_payload_size
                                    0,   // max_payload_count
                                    0);  // max_payload_size
  }

  void FlushInput(bool hold_frame, size_t input_index, fit::closure callback) override {
    callback();
  }

  void FlushOutput(size_t output_index, fit::closure callback) override { callback(); }

  void PutInputPacket(PacketPtr packet, size_t input_index) override { RequestInputPacket(); }

  void RequestOutputPacket() override {}

  void SetInputStreamType(const StreamType& stream_type) override {}

  std::unique_ptr<StreamType> output_stream_type() const override {
    FX_DCHECK(output_stream_type_);
    return output_stream_type_->Clone();
  }

 private:
  std::unique_ptr<StreamType> output_stream_type_;
};

// Tests that we can destroy the async loop with the AudioRenderer connection in place without
// panicking.
TEST(FidlAudioRendererTest, DestroyLoopWithoutDisconnecting) {
  std::shared_ptr<FidlAudioRenderer> under_test;
  FakeAudioRenderer fake_audio_renderer;

  {
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

    fuchsia::media::AudioRendererPtr fake_audio_renderer_ptr;
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request =
        fake_audio_renderer_ptr.NewRequest();
    fake_audio_renderer.Bind(std::move(audio_renderer_request));

    under_test = FidlAudioRenderer::Create(std::move(fake_audio_renderer_ptr));
  }

  // The FidlAudioRenderer still exists at this point and still has a connection to the fake
  // audio renderer. The async loop, however, has gone out of scope.
}

// Converts a pts in |pts_rate| units to ns.
int64_t to_ns(int64_t pts, media::TimelineRate pts_rate) {
  return pts * (media::TimelineRate::NsPerSecond / pts_rate);
}

// Converts a pts in ns to |pts_rate| units.
int64_t from_ns(int64_t pts, media::TimelineRate pts_rate) {
  return pts * (pts_rate / media::TimelineRate::NsPerSecond);
}

using FidlAudioRendererFixture = ::gtest::RealLoopFixture;

/// Test NoPts packet handling to ensure we can recover from potential underrun situation by
/// inserting gaps of silence
///
TEST_F(FidlAudioRendererFixture, DISABLED_DontUnderrunJitteryNoPtsData) {
  syslog::InitLogger({"mediaplayer"});

  static constexpr AudioStreamType::SampleFormat kSampleFormat =
      AudioStreamType::SampleFormat::kSigned16;
  static constexpr uint32_t kChannels = 2;
  static constexpr uint32_t kFramesPerSecond = 48000;
  static constexpr int64_t kSlop = 2;

  Graph graph(dispatcher());
  AudioStreamType stream_type(nullptr, StreamType::kAudioEncodingLpcm, nullptr, kSampleFormat,
                              kChannels, kFramesPerSecond);

  std::shared_ptr<FidlAudioRenderer> under_test;
  FakeAudioRenderer fake_audio_renderer;

  fuchsia::media::AudioRendererPtr fake_audio_renderer_ptr;
  fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request =
      fake_audio_renderer_ptr.NewRequest();
  fake_audio_renderer.Bind(std::move(audio_renderer_request));

  under_test = FidlAudioRenderer::Create(std::move(fake_audio_renderer_ptr));

  // triggers callback to mark renderer as responding
  fake_audio_renderer.EnableMinLeadTimeEvents(true);

  auto fake_decoder = std::make_shared<FakeAudioDecoder>(stream_type);
  auto fake_decoder_node = graph.Add(fake_decoder);

  auto under_test_node = graph.Add(under_test);
  under_test->SetStreamType(stream_type);
  under_test->Provision(dispatcher(), []() {});

  graph.ConnectNodes(fake_decoder_node, under_test_node);

  media::TimelineRate pts_rate = media::TimelineRate(stream_type.audio()->frames_per_second(), 1);

  auto current_timeline_function =
      media::TimelineFunction(0, zx::clock::get_monotonic().get(), media::TimelineRate(1, 1));

  bool timeline_set = false;
  under_test->SetTimelineFunction(current_timeline_function, [&] { timeline_set = true; });

  RunLoopUntil([&]() { return timeline_set; });

  // start priming so packets start flowing
  under_test->Prime([]() {});

  auto buf = PayloadBuffer::CreateWithMalloc(kBufferSize);
  auto pkt = Packet::Create(kNoPts, media::TimelineRate(1, 1), false, false, kBufferSize, buf);

  // test initial lead time stamp
  int64_t now = zx::clock::get_monotonic().get();

  // in frame units
  int64_t expected = from_ns(current_timeline_function(now + kTargetLeadTime), pts_rate);
  bool packet_handler_ran = false;
  fake_audio_renderer.SetPacketHandler(
      [&packet_handler_ran, under_test, expected](fuchsia::media::StreamPacket stream_packet) {
        packet_handler_ran = true;
        EXPECT_NE(stream_packet.pts, kNoPts);
        EXPECT_GE(stream_packet.pts, expected - kSlop);
        EXPECT_LE(stream_packet.pts, expected + kSlop);
      });
  pkt->SetPts(kNoPts);
  under_test->PutInputPacket(pkt, 0);

  RunLoopUntilIdle();
  EXPECT_TRUE(packet_handler_ran);

  // test immediately following
  expected = expected + pkt->size() / stream_type.audio()->bytes_per_frame();
  packet_handler_ran = false;
  fake_audio_renderer.SetPacketHandler(
      [&packet_handler_ran, under_test, expected](fuchsia::media::StreamPacket stream_packet) {
        packet_handler_ran = true;
        EXPECT_NE(stream_packet.pts, kNoPts);
        EXPECT_GE(stream_packet.pts, expected - kSlop);
        EXPECT_LE(stream_packet.pts, expected + kSlop);
      });
  pkt->SetPts(kNoPts);
  under_test->PutInputPacket(pkt, 0);
  RunLoopUntilIdle();
  EXPECT_TRUE(packet_handler_ran);

  // test gap, actual sleep is needed since the audio renderer looks at clock monotonic to
  // determine the gap
  zx_time_t deadline = zx_deadline_after(kTargetLeadTime * 2);
  zx_nanosleep(deadline);

  now = zx::clock::get_monotonic().get();
  expected = from_ns(current_timeline_function(now + kNoPtsSlipOnStarveNs), pts_rate);
  packet_handler_ran = false;
  fake_audio_renderer.SetPacketHandler(
      [&packet_handler_ran, under_test, expected](fuchsia::media::StreamPacket stream_packet) {
        packet_handler_ran = true;
        EXPECT_NE(stream_packet.pts, kNoPts);
        EXPECT_GE(stream_packet.pts, expected - kSlop);
        EXPECT_LE(stream_packet.pts, expected + kSlop);
      });
  pkt->SetPts(kNoPts);
  under_test->PutInputPacket(pkt, 0);
  RunLoopUntilIdle();
  EXPECT_TRUE(packet_handler_ran);
}

}  // namespace test
}  // namespace media_player
