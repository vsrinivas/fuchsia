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

#include "src/media/audio/audio_core/test/pipeline/hermetic_audio_pipeline_test.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"

namespace media::audio::test {

class AudioPipelineTest : public HermeticAudioPipelineTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioPipelineTest::SetUpTestSuite(HermeticAudioEnvironment::Options());
  }
  static void TearDownTestSuite() { HermeticAudioPipelineTest::TearDownTestSuite(); }
};

// Validate that timestamped packets play through renderer to ring buffer as expected.
TEST_F(AudioPipelineTest, RenderWithPts) {
  ASSERT_GT(GetMinLeadTime(), 0);
  uint32_t num_packets = zx::duration(GetMinLeadTime()) / zx::msec(kPacketMs);
  ++num_packets;

  auto input_buffer = GenerateSequentialAudio(num_packets);
  SendPackets(num_packets);
  SynchronizedPlay();

  // Let all packets play through the system (including an extra silent packet).
  WaitForPacket(num_packets);
  auto ring_buffer = CreateSnapshotOfRingBuffer();

  // The ring buffer should match the input buffer for the first num_packets.
  // The remaining bytes should be zeros.
  auto num_frames = num_packets * kPacketFrames;
  test_phase_ = "check data";
  CheckRingBuffer(AudioBufferSlice(&ring_buffer, 0, num_frames),
                  AudioBufferSlice(&input_buffer, 0, num_frames));
  test_phase_ = "check silence";
  CheckRingBuffer(AudioBufferSlice(&ring_buffer, num_frames, kRingFrames), AudioBufferSlice());
}

// If we issue DiscardAllPackets during Playback, PTS should not change.
TEST_F(AudioPipelineTest, DiscardDuringPlayback) {
  ASSERT_GT(GetMinLeadTime(), 0);
  const auto packet_offset_delay = (GetMinLeadTime() / ZX_MSEC(kPacketMs)) + 1;
  const auto pts_offset_delay = packet_offset_delay * kPacketFrames;

  uint32_t num_packets = kNumPayloads - 1;
  auto first_input = GenerateSequentialAudio(num_packets);
  SendPackets(num_packets);
  SynchronizedPlay();

  // Load the renderer with lots of packets, but interrupt after a couple of them.
  WaitForPacket(1);

  auto received_discard_all_callback = false;
  audio_renderer_->DiscardAllPackets(CompletionCallback([&received_discard_all_callback]() {
    received_discard_all_callback = true;
    AUD_VLOG(TRACE) << "DiscardAllPackets #1 complete";
  }));
  RunLoopUntil([this, &received_discard_all_callback]() {
    return (error_occurred_ || received_discard_all_callback);
  });

  // The entire first two packets must have been written. Subsequent packets may have been partially
  // written, depending on exactly when the DiscardAllPackets command is received. The remaining
  // bytes should be zeros.
  auto ring_buffer = CreateSnapshotOfRingBuffer();
  test_phase_ = "first_input, first packet";
  CheckRingBuffer(AudioBufferSlice(&ring_buffer, 0, 2 * kPacketFrames),
                  AudioBufferSlice(&first_input, 0, 2 * kPacketFrames));
  test_phase_ = "first_input, third packet onwards";
  CheckRingBufferPartial(AudioBufferSlice(&ring_buffer, 2 * kPacketFrames, kRingFrames),
                         AudioBufferSlice(&first_input, 2 * kPacketFrames, kRingFrames));

  // After interrupting the stream without stopping, now play another sequence of packets starting
  // at least "min_lead_time" after the last audio frame previously written to the ring buffer.
  // Between Left|Right, initial data values were odd|even; these are even|odd, for quick contrast
  // when visually inspecting the buffer.
  int16_t restart_data_value = 0x4000;
  int64_t restart_pts = 2 * kPacketFrames + pts_offset_delay;
  auto second_input = GenerateSequentialAudio(num_packets, restart_data_value);
  SendPackets(num_packets, restart_pts);
  WaitForPacket(num_packets);  // wait for an extra silent packet as well

  // The ring buffer should contain first_input for 10ms (one packet), then partially-written data
  // followed by zeros until restart_pts, then second_input (num_packets), then the remaining bytes
  // should be zeros.
  ring_buffer = CreateSnapshotOfRingBuffer();

  test_phase_ = "first packet, after the second write";
  CheckRingBuffer(AudioBufferSlice(&ring_buffer, 0, 2 * kPacketFrames),
                  AudioBufferSlice(&first_input, 0, 2 * kPacketFrames));

  test_phase_ = "space between the first packet and second_input";
  CheckRingBufferPartial(AudioBufferSlice(&ring_buffer, 2 * kPacketFrames, restart_pts),
                         AudioBufferSlice(&first_input, 2 * kPacketFrames, restart_pts));

  test_phase_ = "second_input";
  CheckRingBuffer(
      AudioBufferSlice(&ring_buffer, restart_pts, restart_pts + num_packets * kPacketFrames),
      AudioBufferSlice(&second_input, 0, num_packets * kPacketFrames));

  test_phase_ = "silence after second_input";
  CheckRingBuffer(
      AudioBufferSlice(&ring_buffer, restart_pts + num_packets * kPacketFrames, kRingFrames),
      AudioBufferSlice());
}

class AudioPipelineEffectsTest : public HermeticAudioPipelineTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioPipelineTest::SetUpTestSuite(HermeticAudioEnvironment::Options{
        .audio_core_base_url = "fuchsia-pkg://fuchsia.com/audio-core-for-pipeline-tests",
        .audio_core_config_data_path = "/pkg/data/audio_core_config_with_inversion_filter",
    });
  }

  void RunInversionFilter(AudioBuffer* audio_buffer_ptr) {
    AudioBuffer& audio_buffer = *audio_buffer_ptr;
    for (size_t sample = 0; sample < audio_buffer.size(); sample++) {
      audio_buffer[sample] = -audio_buffer[sample];
    }
  }
};

// Validate that the effects package is loaded and that it processes the input.
TEST_F(AudioPipelineEffectsTest, RenderWithEffects) {
  ASSERT_GT(GetMinLeadTime(), 0);
  uint32_t num_packets = zx::duration(GetMinLeadTime()) / zx::msec(kPacketMs);
  ++num_packets;

  auto input_buffer = GenerateSequentialAudio(num_packets);
  SendPackets(num_packets);
  SynchronizedPlay();

  // Let all packets play through the system (including an extra silent packet).
  WaitForPacket(num_packets);
  auto ring_buffer = CreateSnapshotOfRingBuffer();

  // Simulate running the effect on the input buffer.
  RunInversionFilter(&input_buffer);

  // The ring buffer should match the transformed input buffer for the first num_packets.
  // The remaining bytes should be zeros.
  auto num_frames = num_packets * kPacketFrames;
  test_phase_ = "check data";
  CheckRingBuffer(AudioBufferSlice(&ring_buffer, 0, num_frames),
                  AudioBufferSlice(&input_buffer, 0, num_frames));
  test_phase_ = "check silence";
  CheckRingBuffer(AudioBufferSlice(&ring_buffer, num_frames, kRingFrames), AudioBufferSlice());
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
