// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_renderer.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v1/audio_admin.h"
#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/stream_volume_manager.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/v1/throttle_output.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

constexpr uint32_t kAudioRendererUnittestFrameRate = 48000;
constexpr size_t kAudioRendererUnittestVmoSize = 16ull * 1024;

class AudioRendererTest : public testing::ThreadingModelFixture {
 public:
  explicit AudioRendererTest(ClockFactoryMode mode) : testing::ThreadingModelFixture(mode) {
    FX_CHECK(vmo_mapper_.CreateAndMap(kAudioRendererUnittestVmoSize,
                                      /*flags=*/0, nullptr, &vmo_) == ZX_OK);
  }

 protected:
  void SetUp() override {
    testing::ThreadingModelFixture::SetUp();

    renderer_ = AudioRenderer::Create(fidl_renderer_.NewRequest(), &context());
    EXPECT_NE(renderer_.get(), nullptr);

    fidl_renderer_.set_error_handler(
        [](auto status) { EXPECT_TRUE(status == ZX_OK) << "Renderer disconnected: " << status; });

    fake_output_ = testing::FakeAudioOutput::Create(
        context().process_config().device_config(), &threading_model(), &context().device_manager(),
        &context().link_matrix(), context().clock_factory());
  }

  fuchsia::media::AudioStreamType PcmStreamType() {
    return fuchsia::media::AudioStreamType{
        .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
        .channels = 1,
        .frames_per_second = kAudioRendererUnittestFrameRate,
    };
  }

  // Creates a new payload buffer of |size| bytes and registers it with the renderer with |id|.
  //
  // A handle to the new VMO is returned.
  zx::vmo AddPayloadBuffer(uint32_t id, size_t size, fuchsia::media::AudioRenderer* renderer) {
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, zx::vmo::create(size, 0, &vmo));

    zx::vmo duplicate;
    EXPECT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate));
    renderer->AddPayloadBuffer(id, std::move(duplicate));
    RunLoopUntilIdle();
    return vmo;
  }

  void TearDown() override {
    // Dropping the channel queues up a reference to the Renderer through its error handler, which
    // will not work since the rest of this class is destructed before the loop and its
    // queued functions are. Here, we ensure the error handler runs before this class' destructors
    // run.
    { auto r = std::move(fidl_renderer_); }
    RunLoopUntilIdle();

    // This ensures that the device is properly unwired from RouteGraph etc., before ~AudioDevice.
    context().device_manager().RemoveDevice(fake_output_);

    testing::ThreadingModelFixture::TearDown();
  }

  zx::clock GetReferenceClock() {
    zx::clock fidl_clock;
    fidl_renderer_->GetReferenceClock(
        [&fidl_clock](zx::clock ref_clock) { fidl_clock = std::move(ref_clock); });
    RunLoopUntilIdle();

    return fidl_clock;
  }

  fuchsia::media::AudioRendererPtr fidl_renderer_;
  std::shared_ptr<AudioRenderer> renderer_;

  std::shared_ptr<testing::FakeAudioOutput> fake_output_;

  fzl::VmoMapper vmo_mapper_;
  zx::vmo vmo_;

  fuchsia::media::AudioStreamType stream_type_ = {
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 1,
      .frames_per_second = kAudioRendererUnittestFrameRate,
  };
};

// Real clocks are needed for tests that call GetReferenceClock.
class AudioRendererTestRealClocks : public AudioRendererTest {
 public:
  AudioRendererTestRealClocks() : AudioRendererTest(WithRealClocks) {}
};

// Synthetic clocks may be used in other tests.
class AudioRendererTestSyntheticClocks : public AudioRendererTest {
 public:
  AudioRendererTestSyntheticClocks() : AudioRendererTest(WithSyntheticClocks) {}
};

constexpr zx::duration kMinLeadTime = zx::nsec(123456789);
constexpr int64_t kInvalidLeadTimeNs = -1;

// Validate that MinLeadTime is provided to AudioRenderer clients accurately
TEST_F(AudioRendererTestSyntheticClocks, MinLeadTimePadding) {
  // We must set our output's delay, before linking it, before calling SetPcmStreamType().
  fake_output_->SetPresentationDelay(kMinLeadTime);

  // Our RouteGraph links one FakeAudioOutput to the Renderer-under-test. Thus we can set
  // our output's PresentationDelay, fully expecting this value to be reflected as-is to
  // renderer+clients.
  context().route_graph().AddRenderer(std::move(renderer_));
  context().route_graph().AddDeviceToRoutes(fake_output_.get());

  // SetPcmStreamType triggers the routing preparation completion, which connects output(s) to
  // renderer. Renderers react to new outputs in `OnLinkAdded` by recalculating minimum lead time.
  fidl_renderer_->SetPcmStreamType(PcmStreamType());
  RunLoopUntilIdle();

  auto lead_time_ns = kInvalidLeadTimeNs;
  fidl_renderer_->GetMinLeadTime(
      [&lead_time_ns](int64_t received_lead_time_ns) { lead_time_ns = received_lead_time_ns; });

  RunLoopUntilIdle();
  ASSERT_NE(lead_time_ns, kInvalidLeadTimeNs) << "No response received for GetMinLeadTime";
  EXPECT_EQ(lead_time_ns, kMinLeadTime.to_nsecs()) << "Incorrect GetMinLeadTime received";
}

TEST_F(AudioRendererTestSyntheticClocks, AllocatePacketQueueForLinks) {
  context().route_graph().AddRenderer(std::move(renderer_));
  context().route_graph().AddDeviceToRoutes(fake_output_.get());

  const size_t kFrames = 16;
  fidl_renderer_->SetPcmStreamType(PcmStreamType());
  AddPayloadBuffer(0, zx_system_get_page_size(), fidl_renderer_.get());
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = kFrames * sizeof(float);
  fidl_renderer_->SendPacketNoReply(std::move(packet));
  RunLoopUntilIdle();

  std::vector<LinkMatrix::LinkHandle> links;
  context().link_matrix().SourceLinks(*fake_output_, &links);
  ASSERT_EQ(1u, links.size());
  for (auto& link : links) {
    auto stream = link.stream;
    ASSERT_TRUE(stream);

    {  // Expect a buffer.
      auto buffer = stream->ReadLock(rlctx, Fixed(0), kFrames);
      ASSERT_TRUE(buffer);
      EXPECT_NE(nullptr, buffer->payload());
    }
    {  // No more buffers.
      auto buffer = stream->ReadLock(rlctx, Fixed(kFrames), 10);
      ASSERT_FALSE(buffer);
    }
  }
}

TEST_F(AudioRendererTestSyntheticClocks, SendPacket_NO_TIMESTAMP) {
  context().route_graph().AddRenderer(std::move(renderer_));
  context().route_graph().AddDeviceToRoutes(fake_output_.get());

  fidl_renderer_->SetPcmStreamType(PcmStreamType());
  AddPayloadBuffer(0, zx_system_get_page_size(), fidl_renderer_.get());
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = 128;
  fidl_renderer_->SendPacketNoReply(fidl::Clone(packet));
  fidl_renderer_->SendPacketNoReply(fidl::Clone(packet));
  fidl_renderer_->SendPacketNoReply(fidl::Clone(packet));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  RunLoopUntilIdle();

  std::vector<LinkMatrix::LinkHandle> links;
  context().link_matrix().SourceLinks(*fake_output_, &links);
  ASSERT_EQ(1u, links.size());
  auto stream = links[0].stream;
  ASSERT_TRUE(stream);

  // Expect 3 buffers. Since these have NO_TIMESTAMP and also no discontinutity flag, they should
  // be continuous starting at pts 0.
  constexpr int64_t kPacketSizeFrames = 32;
  int64_t expected_packet_pts = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    auto buffer = stream->ReadLock(rlctx, Fixed(expected_packet_pts), kPacketSizeFrames);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start().Floor(), expected_packet_pts);
    EXPECT_EQ(buffer->length(), kPacketSizeFrames);
    EXPECT_NE(nullptr, buffer->payload());
    expected_packet_pts = buffer->end().Floor();
  }

  // Send another set of packets after lead time + padding to ensure these packets cannot be played
  // continuously with the last set of packets. Now we use FLAG_DISCONTINUITY which means they
  // will not be continuous with the previous packets.
  context().clock_factory()->AdvanceMonoTimeBy(stream->GetPresentationDelay() + zx::msec(30));
  packet.flags |= fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY;
  fidl_renderer_->SendPacketNoReply(fidl::Clone(packet));
  fidl_renderer_->SendPacketNoReply(fidl::Clone(packet));
  fidl_renderer_->SendPacketNoReply(fidl::Clone(packet));
  RunLoopUntilIdle();
  {
    // Read enough frames to include all three packets in the same buffer.
    // The buffer should appear PresentationDelay+30ms after expected_packet_pts.
    auto delay_ms = stream->GetPresentationDelay().to_msecs() + 30 + 1 /* round up */;
    auto total_packets = 3 * kPacketSizeFrames + delay_ms * kAudioRendererUnittestFrameRate / 1000;
    auto buffer = stream->ReadLock(rlctx, Fixed(expected_packet_pts), total_packets);
    ASSERT_TRUE(buffer);
    // GT here as we are not continuous with the previous packet.
    EXPECT_GT(buffer->start().Floor(), expected_packet_pts);
    EXPECT_EQ(buffer->length(), kPacketSizeFrames);
    EXPECT_NE(nullptr, buffer->payload());
    expected_packet_pts = buffer->end().Floor();
  }

  for (uint32_t i = 0; i < 2; ++i) {
    auto buffer = stream->ReadLock(rlctx, Fixed(expected_packet_pts), kPacketSizeFrames);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->start().Floor(), expected_packet_pts);
    EXPECT_EQ(buffer->length(), kPacketSizeFrames);
    EXPECT_NE(nullptr, buffer->payload());
    expected_packet_pts = buffer->end().Floor();
  }
}

// The renderer should be routed once the format is set.
TEST_F(AudioRendererTestSyntheticClocks, RegistersWithRouteGraphIfHasUsageStreamTypeAndBuffers) {
  EXPECT_EQ(context().link_matrix().DestLinkCount(*renderer_), 0u);

  zx::vmo duplicate;
  ASSERT_EQ(
      vmo_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_READ | ZX_RIGHT_MAP, &duplicate),
      ZX_OK);

  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  auto* renderer_raw = renderer_.get();
  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  RunLoopUntilIdle();
  EXPECT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 0u);

  fidl_renderer_->SetPcmStreamType(stream_type_);
  RunLoopUntilIdle();
  EXPECT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 1u);

  fidl_renderer_->AddPayloadBuffer(0, std::move(duplicate));

  RunLoopUntilIdle();
  EXPECT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 1u);
}

// AudioRenderer should survive, if it calls Play while already playing.
TEST_F(AudioRendererTestSyntheticClocks, DoublePlay) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                       [](int64_t ref_time, int64_t media_time) {
                         EXPECT_NE(ref_time, fuchsia::media::NO_TIMESTAMP);
                         EXPECT_NE(media_time, fuchsia::media::NO_TIMESTAMP);
                       });
  RunLoopFor(zx::msec(20));  // wait for any Play-related pended actions to try to complete

  fidl_renderer_->Play(fuchsia::media::NO_TIMESTAMP, 0, [](int64_t ref_time, int64_t media_time) {
    EXPECT_NE(ref_time, fuchsia::media::NO_TIMESTAMP);
    EXPECT_NE(media_time, fuchsia::media::NO_TIMESTAMP);
  });
  RunLoopFor(zx::msec(20));  // wait for any Play-related pended actions to try to complete

  EXPECT_TRUE(fidl_renderer_.is_bound());
}

// AudioRenderer should survive, if it calls Pause for a second time before calling Play.
// Timestamps returned from this second Pause should be the same as those from the first.
TEST_F(AudioRendererTestSyntheticClocks, DoublePause) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);
  RunLoopUntilIdle();

  int64_t received_ref_time = fuchsia::media::NO_TIMESTAMP;
  int64_t received_media_time = fuchsia::media::NO_TIMESTAMP;
  fidl_renderer_->Pause(
      [&received_ref_time, &received_media_time](int64_t ref_time, int64_t media_time) {
        EXPECT_NE(ref_time, fuchsia::media::NO_TIMESTAMP);
        EXPECT_NE(media_time, fuchsia::media::NO_TIMESTAMP);

        received_ref_time = ref_time;
        received_media_time = media_time;

        FX_LOGS(INFO) << "Received ref_time " << ref_time << ", media_time " << media_time;
      });
  RunLoopFor(zx::msec(20));  // wait for any Pause-related pended actions to try to complete

  fidl_renderer_->Pause(
      [&received_ref_time, &received_media_time](int64_t ref_time, int64_t media_time) {
        EXPECT_NE(ref_time, fuchsia::media::NO_TIMESTAMP);
        EXPECT_NE(media_time, fuchsia::media::NO_TIMESTAMP);

        EXPECT_EQ(received_ref_time, ref_time);
        EXPECT_EQ(received_media_time, media_time);
      });
  RunLoopFor(zx::msec(20));  // wait for any Pause-related pended actions to try to complete

  EXPECT_TRUE(fidl_renderer_.is_bound());
}

// AudioRenderer should survive if calling Pause before ever calling Play.
// We return timestamps that try to indicate that no previous timeline transform was established.
TEST_F(AudioRendererTestSyntheticClocks, PauseBeforePlay) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));

  fidl_renderer_->Pause([](int64_t ref_time, int64_t media_time) {
    EXPECT_EQ(ref_time, fuchsia::media::NO_TIMESTAMP);
    EXPECT_EQ(media_time, fuchsia::media::NO_TIMESTAMP);
  });
  RunLoopFor(zx::msec(20));  // wait for any Pause-related pended actions to try to complete

  EXPECT_TRUE(fidl_renderer_.is_bound());
}

TEST_F(AudioRendererTestSyntheticClocks, ReportsPlayAndPauseToPolicy) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));

  bool received_play_callback = false;
  fidl_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                       [&received_play_callback](int64_t ref_time, int64_t media_time) {
                         received_play_callback = true;
                       });

  auto run_loop_count = 0;
  while (!received_play_callback && run_loop_count < 100) {
    RunLoopFor(zx::msec(5));
    ++run_loop_count;
  }
  EXPECT_TRUE(context().audio_admin().IsActive(RenderUsage::SYSTEM_AGENT));

  bool received_pause_callback = false;
  fidl_renderer_->Pause([&received_pause_callback](int64_t ref_time, int64_t media_time) {
    received_pause_callback = true;
  });

  run_loop_count = 0;
  while (!received_pause_callback && run_loop_count < 100) {
    RunLoopFor(zx::msec(5));
    ++run_loop_count;
  }
  EXPECT_FALSE(context().audio_admin().IsActive(RenderUsage::SYSTEM_AGENT));
}

// AudioCore should survive, if a renderer is unbound between a Play call and its callback.
TEST_F(AudioRendererTestSyntheticClocks, RemoveRendererDuringPlay) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                       [](int64_t ref_time, int64_t media_time) {
                         FX_LOGS(INFO)
                             << "Play callback: ref " << ref_time << ", media " << media_time;
                       });

  // Simulate closing the client binding. This will shutdown the renderer.
  fidl_renderer_.Unbind();
  RunLoopFor(zx::msec(20));  // wait for any Play-related pended actions to try to complete
}

// AudioCore should survive, if a renderer is unbound immediately after PlayNoReply, as AudioCore
// may kick off deferred actions that need to be safely retired.
TEST_F(AudioRendererTestSyntheticClocks, RemoveRendererDuringPlayNoReply) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  // Simulate closing the client binding. This will shutdown the renderer.
  fidl_renderer_.Unbind();
  RunLoopFor(zx::msec(20));  // wait for any Play-related pended actions to try to complete
}

// AudioCore should survive, if a renderer is unbound between a Pause call and its callback.
TEST_F(AudioRendererTestSyntheticClocks, RemoveRendererDuringPause) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  fidl_renderer_->Pause([](int64_t ref_time, int64_t media_time) {
    FX_LOGS(INFO) << "Pause callback: ref " << ref_time << ", media " << media_time;
  });

  // Simulate closing the client binding. This will shutdown the renderer.
  fidl_renderer_.Unbind();
  RunLoopFor(zx::msec(20));  // wait for any Pause-related pended actions to try to complete
}

// AudioCore should survive, if a renderer is unbound immediately after PauseNoReply, as AudioCore
// may kick off deferred actions that need to be safely retired.
TEST_F(AudioRendererTestSyntheticClocks, RemoveRendererDuringPauseNoReply) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  fidl_renderer_->PauseNoReply();

  // Simulate closing the client binding. This will shutdown the renderer.
  fidl_renderer_.Unbind();
  RunLoopFor(zx::msec(20));  // wait for any Pause-related pended actions to try to complete
}

TEST_F(AudioRendererTestSyntheticClocks, RemoveRendererWhileBufferLocked) {
  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  context().route_graph().AddRenderer(std::move(renderer_));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(vmo_));
  fidl_renderer_->PlayNoReply(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP);

  // Enqueue a packet
  fuchsia::media::StreamPacket packet;
  packet.pts = fuchsia::media::NO_TIMESTAMP;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_size = 128;
  fidl_renderer_->SendPacketNoReply(std::move(packet));
  RunLoopUntilIdle();

  // This will be the packet queue created when the link between the renderer and output was formed.
  auto packet_queue = fake_output_->stream();
  ASSERT_TRUE(packet_queue);

  // Acquire a buffer.
  auto buf = packet_queue->ReadLock(rlctx, Fixed(0), 32);
  ASSERT_TRUE(buf);
  EXPECT_EQ(buf->start().Floor(), 0);
  EXPECT_EQ(buf->length(), 32);

  // Simulate closing the client binding. This will shutdown the renderer.
  fidl_renderer_.Unbind();
  RunLoopUntilIdle();

  // Now release the buffer.
  buf = std::nullopt;
  RunLoopUntilIdle();
}

TEST_F(AudioRendererTestRealClocks, ReferenceClockIsAdvancing) {
  auto fidl_clock = GetReferenceClock();
  clock::testing::VerifyAdvances(fidl_clock);
  clock::testing::VerifyAdvances(*renderer_->reference_clock());
}

TEST_F(AudioRendererTestRealClocks, GetReferenceClockReturnsReadOnlyClock) {
  auto fidl_clock = GetReferenceClock();
  clock::testing::VerifyCannotBeRateAdjusted(fidl_clock);
}

TEST_F(AudioRendererTestRealClocks, DefaultClockIsInitiallyMonotonic) {
  auto fidl_clock = GetReferenceClock();
  clock::testing::VerifyIsSystemMonotonic(fidl_clock);
  clock::testing::VerifyIsSystemMonotonic(*renderer_->reference_clock());
}

TEST_F(AudioRendererTestRealClocks, DefaultClockIsFlexible) {
  clock::testing::VerifyCanBeRateAdjusted(*renderer_->reference_clock());
  EXPECT_TRUE(renderer_->reference_clock()->adjustable());
  // Must be a client clock, which should always be in the "external" domain.
  EXPECT_EQ(renderer_->reference_clock()->domain(), Clock::kExternalDomain);
}

// The renderer clock is valid, before and after devices are routed.
TEST_F(AudioRendererTestRealClocks, ReferenceClockIsCorrectAfterDeviceChange) {
  auto* renderer_raw = renderer_.get();
  context().route_graph().AddRenderer(std::move(renderer_));
  RunLoopUntilIdle();

  auto fidl_clock = GetReferenceClock();

  fidl_renderer_->SetPcmStreamType(stream_type_);
  RunLoopUntilIdle();
  ASSERT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 1u);

  context().route_graph().AddDeviceToRoutes(fake_output_.get());
  RunLoopUntilIdle();

  ASSERT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 1u);
  clock::testing::VerifyAdvances(fidl_clock);
  clock::testing::VerifyIsSystemMonotonic(fidl_clock);
  clock::testing::VerifyCannotBeRateAdjusted(fidl_clock);

  // Remove the device and validate that the reference clock received from the renderer remains
  // valid, even after the device is removed (and thus renderer is rerouted to a ThrottleOutput)
  context().route_graph().RemoveDeviceFromRoutes(fake_output_.get());
  // device_manager will call RemoveDevice again during TearDown, which is benign

  RunLoopUntilIdle();
  ASSERT_EQ(context().link_matrix().DestLinkCount(*renderer_raw), 1u);
  clock::testing::VerifyAdvances(fidl_clock);
  clock::testing::VerifyIsSystemMonotonic(fidl_clock);
  clock::testing::VerifyCannotBeRateAdjusted(fidl_clock);
}

}  // namespace
}  // namespace media::audio
