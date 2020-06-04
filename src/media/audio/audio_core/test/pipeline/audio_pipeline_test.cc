// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/comparators.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

constexpr size_t kNumPacketsInPayload = 50;
constexpr size_t kFrameRate = 48000;
constexpr size_t kPacketFrames = kFrameRate / 1000 * RendererShimImpl::kPacketMs;
constexpr size_t kPayloadFrames = kPacketFrames * kNumPacketsInPayload;

constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
    fuchsia::media::AudioSampleFormat::SIGNED_16;

class AudioPipelineTest : public HermeticAudioTest {
 protected:
  AudioPipelineTest()
      : format_(Format::Create({
                                   .sample_format = kSampleFormat,
                                   .channels = 2,
                                   .frames_per_second = kFrameRate,
                               })
                    .value()) {}

  void SetUp() {
    HermeticAudioTest::SetUp();
    // None of our tests should underflow.
    FailUponUnderflows();
    // The output and renderer can both store exactly 1s of audio data.
    output_ = CreateOutput<kSampleFormat>({{0xff, 0x00}}, format_, 48000);
    renderer_ = CreateAudioRenderer<kSampleFormat>(format_, kPayloadFrames);
  }

  const Format format_;
  VirtualOutput<kSampleFormat>* output_ = nullptr;
  AudioRendererShim<kSampleFormat>* renderer_ = nullptr;
};

// Validate that timestamped packets play through renderer to ring buffer as expected.
TEST_F(AudioPipelineTest, RenderWithPts) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  auto num_packets = zx::duration(min_lead_time) / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<kSampleFormat>(format_, num_frames);
  renderer_->AppendPayload(&input_buffer);
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  auto silent_packet = GenerateSilentAudio<kSampleFormat>(format_, kPacketFrames);
  renderer_->AppendPayload(&silent_packet);
  renderer_->SendPackets(num_packets + 1);
  renderer_->Play(this, output_->NextSynchronizedTimestamp(this), 0);

  // Let all packets play through the system (including an extra silent packet).
  renderer_->WaitForPacket(this, num_packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // The ring buffer should match the input buffer for the first num_packets.
  // The remaining bytes should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<kSampleFormat>(), opts);
}

// If we issue DiscardAllPackets during Playback, PTS should not change.
TEST_F(AudioPipelineTest, DiscardDuringPlayback) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  // Add 2 extra packets to allow for scheduling delay to reduce flakes. See fxb/52410.
  constexpr auto kSchedulingDelayInPackets = 2;
  const auto packet_offset_delay =
      (min_lead_time / ZX_MSEC(RendererShimImpl::kPacketMs)) + kSchedulingDelayInPackets;
  const auto pts_offset_delay = packet_offset_delay * kPacketFrames;

  auto num_packets = kNumPacketsInPayload - 1;
  auto num_frames = num_packets * kPacketFrames;

  auto first_input = GenerateSequentialAudio<kSampleFormat>(format_, num_packets);
  renderer_->AppendPayload(&first_input);
  renderer_->SendPackets(num_packets);
  renderer_->Play(this, output_->NextSynchronizedTimestamp(this), 0);

  // Load the renderer with lots of packets, but interrupt after two of them.
  renderer_->WaitForPacket(this, 1);

  auto received_discard_all_callback = false;
  renderer_->renderer()->DiscardAllPackets(CompletionCallback([&received_discard_all_callback]() {
    received_discard_all_callback = true;
    AUD_VLOG(TRACE) << "DiscardAllPackets #1 complete";
  }));
  RunLoopUntil([this, &received_discard_all_callback]() {
    return (error_occurred_ || received_discard_all_callback);
  });

  // The entire first two packets must have been written. Subsequent packets may have been partially
  // written, depending on exactly when the DiscardAllPackets command is received. The remaining
  // bytes should be zeros.
  auto ring_buffer = output_->SnapshotRingBuffer();
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "first_input, first packet";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, 2 * kPacketFrames),
                      AudioBufferSlice(&first_input, 0, 2 * kPacketFrames), opts);
  opts.test_label = "first_input, third packet onwards";
  opts.partial = true;
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 2 * kPacketFrames, output_->frame_count()),
                      AudioBufferSlice(&first_input, 2 * kPacketFrames, output_->frame_count()),
                      opts);

  opts.partial = false;

  // After interrupting the stream without stopping, now play another sequence of packets starting
  // at least "min_lead_time" after the last audio frame previously written to the ring buffer.
  // Between Left|Right, initial data values were odd|even; these are even|odd, for quick contrast
  // when visually inspecting the buffer.
  int16_t restart_data_value = 0x4000;
  int64_t restart_pts = 2 * kPacketFrames + pts_offset_delay;
  auto second_input =
      GenerateSequentialAudio<kSampleFormat>(format_, num_frames, restart_data_value);
  auto silent_packet = GenerateSilentAudio<kSampleFormat>(format_, kPacketFrames);
  renderer_->ClearPayload();
  renderer_->AppendPayload(&second_input);
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  renderer_->AppendPayload(&silent_packet);
  renderer_->SendPackets(num_packets + 1, restart_pts);
  renderer_->WaitForPacket(this, num_packets);  // wait for the extra silent packet as well

  // The ring buffer should contain first_input for 10ms (one packet), then partially-written data
  // followed by zeros until restart_pts, then second_input (num_packets), then the remaining bytes
  // should be zeros.
  ring_buffer = output_->SnapshotRingBuffer();

  opts.test_label = "first packet, after the second write";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, 2 * kPacketFrames),
                      AudioBufferSlice(&first_input, 0, 2 * kPacketFrames), opts);

  opts.test_label = "space between the first packet and second_input";
  opts.partial = true;
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 2 * kPacketFrames, restart_pts),
                      AudioBufferSlice(&first_input, 2 * kPacketFrames, restart_pts), opts);

  opts.test_label = "second_input";
  opts.partial = false;
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, restart_pts, restart_pts + num_frames),
                      AudioBufferSlice(&second_input, 0, num_frames), opts);

  opts.test_label = "silence after second_input";
  CompareAudioBuffers(
      AudioBufferSlice(&ring_buffer, restart_pts + num_frames, output_->frame_count()),
      AudioBufferSlice<kSampleFormat>(), opts);
}

class AudioPipelineEffectsTest : public AudioPipelineTest {
 protected:
  // Matches the value in audio_core_config_with_inversion_filter.json
  static constexpr const char* kInverterEffectName = "inverter";

  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-for-pipeline-tests",
        .audio_core_config_data_path = "/pkg/data/audio_core_config_with_inversion_filter",
    });
  }

  void SetUp() override {
    AudioPipelineTest::SetUp();
    environment()->ConnectToService(effects_controller_.NewRequest());
  }

  void RunInversionFilter(AudioBuffer<kSampleFormat>* audio_buffer_ptr) {
    auto& samples = audio_buffer_ptr->samples();
    for (size_t sample = 0; sample < samples.size(); sample++) {
      samples[sample] = -samples[sample];
    }
  }

  fuchsia::media::audio::EffectsControllerSyncPtr effects_controller_;
};

// Validate that the effects package is loaded and that it processes the input.
TEST_F(AudioPipelineEffectsTest, RenderWithEffects) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  auto num_packets = zx::duration(min_lead_time) / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<kSampleFormat>(format_, num_frames);
  renderer_->AppendPayload(&input_buffer);
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  auto silent_packet = GenerateSilentAudio<kSampleFormat>(format_, kPacketFrames);
  renderer_->AppendPayload(&silent_packet);
  renderer_->SendPackets(num_packets + 1);
  renderer_->Play(this, output_->NextSynchronizedTimestamp(this), 0);

  // Let all packets play through the system (including an extra silent packet).
  renderer_->WaitForPacket(this, num_packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // Simulate running the effect on the input buffer.
  RunInversionFilter(&input_buffer);

  // The ring buffer should match the transformed input buffer for the first num_packets.
  // The remaining bytes should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<kSampleFormat>(), opts);
}

TEST_F(AudioPipelineEffectsTest, EffectsControllerEffectDoesNotExist) {
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller_->UpdateEffect("invalid_effect_name", "disable", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
}

TEST_F(AudioPipelineEffectsTest, EffectsControllerInvalidConfig) {
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status =
      effects_controller_->UpdateEffect(kInverterEffectName, "invalid config string", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG);
}

// Similar to RenderWithEffects, except we send a message to the effect to ask it to disable
// processing.
TEST_F(AudioPipelineEffectsTest, EffectsControllerUpdateEffect) {
  // Disable the inverter; frames should be unmodified.
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller_->UpdateEffect(kInverterEffectName, "disable", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_response());

  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  auto num_packets = zx::duration(min_lead_time) / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<kSampleFormat>(format_, num_frames);
  renderer_->AppendPayload(&input_buffer);
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  auto silent_packet = GenerateSilentAudio<kSampleFormat>(format_, kPacketFrames);
  renderer_->AppendPayload(&silent_packet);
  renderer_->SendPackets(num_packets + 1);
  renderer_->Play(this, output_->NextSynchronizedTimestamp(this), 0);

  // Let all packets play through the system (including an extra silent packet).
  renderer_->WaitForPacket(this, num_packets);
  auto ring_buffer = output_->SnapshotRingBuffer();

  // The ring buffer should match the input buffer for the first num_packets. The remaining bytes
  // should be zeros.
  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, 0, num_frames),
                      AudioBufferSlice(&input_buffer, 0, num_frames), opts);
  opts.test_label = "check silence";
  CompareAudioBuffers(AudioBufferSlice(&ring_buffer, num_frames, output_->frame_count()),
                      AudioBufferSlice<kSampleFormat>(), opts);
}

// /// Overall, need to add tests to validate various Renderer pipeline aspects
// TODO(mpuryear): validate the combinations of NO_TIMESTAMP (Play ref_time,
//     Play media_time, packet PTS)
// TODO(mpuryear): validate gain and ramping
// TODO(mpuryear): validate frame-rate, and fractional position
// TODO(mpuryear): validate channelization (future)
// TODO(mpuryear): validate sample format
// TODO(mpuryear): validate timing/sequence/latency of all callbacks
// TODO(mpuryear): validate various permutations of PtsUnits. Ref clocks?
// TODO(mpuryear): handle EndOfStream?
// TODO(mpuryear): test >1 payload buffer
// TODO(mpuryear): test late packets (no timestamps), gap-then-signal at driver.
//     Should include various permutations of MinLeadTime, ContinuityThreshold
// TODO(mpuryear): test packets with timestamps already played -- expect
//     truncated-signal at driver
// TODO(mpuryear): test packets with timestamps too late -- expect Renderer
//     gap-then-truncated-signal at driver
// TODO(mpuryear): test that no data is lost when Renderer Play-Pause-Play

////// Need to add similar tests for the Capture pipeline
// TODO(mpuryear): validate signal gets bit-for-bit from driver to capturer
// TODO(mpuryear): test OnPacketProduced timing etc.
// TODO(mpuryear): test OnEndOfStream
// TODO(mpuryear): test ReleasePacket
// TODO(mpuryear): test DiscardAllPackets timing etc.
// TODO(mpuryear): test DiscardAllPacketsNoReply timing etc.

}  // namespace media::audio::test
