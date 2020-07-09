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

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

namespace {
constexpr size_t kNumPacketsInPayload = 50;
constexpr size_t kFrameRate = 48000;
constexpr size_t kPacketFrames = kFrameRate / 1000 * RendererShimImpl::kPacketMs;
constexpr size_t kPayloadFrames = kPacketFrames * kNumPacketsInPayload;
}  // namespace

class AudioRendererPipelineTest : public HermeticAudioTest {
 protected:
  AudioRendererPipelineTest() : format_(Format::Create<ASF::SIGNED_16>(2, kFrameRate).value()) {}

  void SetUp() {
    HermeticAudioTest::SetUp();
    // None of our tests should underflow.
    FailUponUnderflows();
    // The output and renderer can both store exactly 1s of audio data.
    output_ = CreateOutput({{0xff, 0x00}}, format_, 48000);
    renderer_ = CreateAudioRenderer(format_, kPayloadFrames);
  }

  const TypedFormat<ASF::SIGNED_16> format_;
  VirtualOutput<ASF::SIGNED_16>* output_ = nullptr;
  AudioRendererShim<ASF::SIGNED_16>* renderer_ = nullptr;
};

// Validate that timestamped packets play through renderer to ring buffer as expected.
TEST_F(AudioRendererPipelineTest, RenderWithPts) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  auto num_packets = zx::duration(min_lead_time) / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  auto start_time = output_->NextSynchronizedTimestamp(this);
  renderer_->Play(this, start_time, 0);

  // Let all packets play through the system (including the extra silent packet).
  renderer_->WaitForPackets(this, start_time, packets);
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
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

// If we issue DiscardAllPackets during Playback, PTS should not change.
TEST_F(AudioRendererPipelineTest, DiscardDuringPlayback) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  // Add extra packets to allow for scheduling delay to reduce flakes in debug mode. See fxb/52410.
  constexpr auto kSchedulingDelayInPackets = 10;
  const auto min_lead_time_in_packets =
      (min_lead_time / ZX_MSEC(RendererShimImpl::kPacketMs)) + kSchedulingDelayInPackets;

  // This test writes to the ring buffer as follows:
  //
  // 1. The first step starts writing num_packets to the front of the ring buffer, but
  //    interrupts and discards after two packets have been written. Because of races,
  //    it's possible that more than two packets will have been written at the moment
  //    the remaining packets are discarded.
  //
  //     +---+---+ ...           +
  //     | P | P | maybe empty   |
  //     +---+---+ ...           +
  //
  //     ^..... num_packets .....^
  //
  // 2. The second step writes another num_packets, starting at least min_lead_time after
  //    the second packet:
  //
  //     +---+---+ ...           +---+ ...               +
  //     | P | P | maybe empty   | P | ...               |
  //     +---+---+ ...           +---+ ...               +
  //
  //             ^ min_lead_time ^
  //             +kSchedulingDelay
  //
  //     ^..... num_packets .....^..... num_packets .....^
  //
  // Note that 1 PTS == 1 frame.
  // To further simplify, all of the above sizes are integer numbers of packets.
  const int64_t first_packet = 0;
  const int64_t restart_packet = 2 + min_lead_time_in_packets;
  const int64_t first_pts = first_packet * kPacketFrames;
  const int64_t restart_pts = restart_packet * kPacketFrames;
  const size_t num_packets = 5;
  const size_t num_frames = num_packets * kPacketFrames;

  // Load the renderer with lots of packets, but interrupt after two of them.
  auto first_input = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto first_packets = renderer_->AppendPackets({&first_input}, first_pts);
  auto first_time = output_->NextSynchronizedTimestamp(this);
  renderer_->Play(this, first_time, 0);
  renderer_->WaitForPackets(this, first_time, {first_packets[0], first_packets[1]});

  renderer_->renderer()->DiscardAllPackets(AddCallback(
      "DiscardAllPackets", []() { AUDIO_LOG(DEBUG) << "DiscardAllPackets #1 complete"; }));
  ExpectCallback();

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
  renderer_->ClearPayload();

  // After interrupting the stream without stopping, now play another sequence of packets starting
  // at least "min_lead_time" after the last audio frame previously written to the ring buffer.
  // Between Left|Right, initial data values were odd|even; these are even|odd, for quick contrast
  // when visually inspecting the buffer.
  const int16_t restart_data_value = 0x4000;
  auto second_input =
      GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames, restart_data_value);
  auto second_packets = renderer_->AppendPackets({&second_input}, restart_pts);
  auto second_time = first_time + ZX_MSEC(restart_packet * RendererShimImpl::kPacketMs);
  renderer_->WaitForPackets(this, second_time, second_packets);

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
      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

class AudioRendererPipelineEffectsTest : public AudioRendererPipelineTest {
 protected:
  // Matches the value in audio_core_config_with_inversion_filter.json
  static constexpr const char* kInverterEffectName = "inverter";

  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-with-inversion-filter",
        .audio_core_config_data_path = "/pkg/data/audio-core-config-with-inversion-filter",
    });
  }

  void SetUp() override {
    AudioRendererPipelineTest::SetUp();
    environment()->ConnectToService(effects_controller_.NewRequest());
  }

  void RunInversionFilter(AudioBuffer<ASF::SIGNED_16>* audio_buffer_ptr) {
    auto& samples = audio_buffer_ptr->samples();
    for (size_t sample = 0; sample < samples.size(); sample++) {
      samples[sample] = -samples[sample];
    }
  }

  fuchsia::media::audio::EffectsControllerSyncPtr effects_controller_;
};

// Validate that the effects package is loaded and that it processes the input.
TEST_F(AudioRendererPipelineEffectsTest, RenderWithEffects) {
  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  auto num_packets = zx::duration(min_lead_time) / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  auto start_time = output_->NextSynchronizedTimestamp(this);
  renderer_->Play(this, start_time, 0);

  // Let all packets play through the system (including the extra silent packet).
  renderer_->WaitForPackets(this, start_time, packets);
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
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
}

TEST_F(AudioRendererPipelineEffectsTest, EffectsControllerEffectDoesNotExist) {
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller_->UpdateEffect("invalid_effect_name", "disable", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
}

TEST_F(AudioRendererPipelineEffectsTest, EffectsControllerInvalidConfig) {
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status =
      effects_controller_->UpdateEffect(kInverterEffectName, "invalid config string", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG);
}

// Similar to RenderWithEffects, except we send a message to the effect to ask it to disable
// processing.
TEST_F(AudioRendererPipelineEffectsTest, EffectsControllerUpdateEffect) {
  // Disable the inverter; frames should be unmodified.
  fuchsia::media::audio::EffectsController_UpdateEffect_Result result;
  zx_status_t status = effects_controller_->UpdateEffect(kInverterEffectName, "disable", &result);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(result.is_response());

  auto min_lead_time = renderer_->GetMinLeadTime();
  ASSERT_GT(min_lead_time, 0);
  auto num_packets = zx::duration(min_lead_time) / zx::msec(RendererShimImpl::kPacketMs);
  auto num_frames = num_packets * kPacketFrames;

  auto input_buffer = GenerateSequentialAudio<ASF::SIGNED_16>(format_, num_frames);
  auto packets = renderer_->AppendPackets({&input_buffer});
  auto start_time = output_->NextSynchronizedTimestamp(this);
  renderer_->Play(this, start_time, 0);

  // Let all packets play through the system (including an extra silent packet).
  renderer_->WaitForPackets(this, start_time, packets);
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
                      AudioBufferSlice<ASF::SIGNED_16>(), opts);
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
