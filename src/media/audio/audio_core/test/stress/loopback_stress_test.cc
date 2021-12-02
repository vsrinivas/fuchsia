// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/media/audio/cpp/types.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/test/comparators.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

using ASF = fuchsia::media::AudioSampleFormat;
using StreamPacket = fuchsia::media::StreamPacket;
using PacketVector = media::audio::test::RendererShimImpl::PacketVector;

namespace media::audio::test {

class AudioLoopbackStressTest : public HermeticAudioTest {
 protected:
  void TearDown() override {
    if constexpr (kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
      ExpectNoOverflowsOrUnderflows();
    } else {
      // We expect no renderer underflows: we pre-submit the whole signal. Keep that check enabled.
      ExpectNoRendererUnderflows();
    }

    HermeticAudioTest::TearDown();
  }

  // Represents a single captured packet.
  struct CapturedPacket {
    int64_t pts;
    AudioBuffer<ASF::SIGNED_24_IN_32> data;
  };

  // Represents a pointer to a specific frame in a vector of packets.
  using PacketAndFrameIdx = std::pair<std::vector<CapturedPacket>::const_iterator, size_t>;

  std::optional<PacketAndFrameIdx> FirstNonSilentFrame(const std::vector<CapturedPacket>& packets) {
    for (auto p = packets.begin(); p != packets.end(); p++) {
      for (auto f = 0; f < p->data.NumFrames(); f++) {
        if (p->data.SampleAt(f, 0)) {
          return std::make_pair(p, f);
        }
      }
    }
    return std::nullopt;
  }
};

// Test that a single long capture produces the correct audio data.
TEST_F(AudioLoopbackStressTest, SingleLongCapture) {
  constexpr auto kChannelCount = 1;
  constexpr auto kFrameRate = 16000;
  const auto kFormat =
      media::audio::Format::Create<ASF::SIGNED_24_IN_32>(kChannelCount, kFrameRate).value();

  constexpr auto kPayloadFrames = kFrameRate;
  constexpr auto kPacketFrames = kFrameRate * 10 / 1000;  // 10ms

  // A longer duration increases the chance of catching bugs in an individual run, but
  // takes more time in CQ. This test will run many times per day, so a smallish number
  // here is fine. As mentioned below, this must be large enough such that the input buffer
  // is larger than all buffers inside audio_core, which are typically <= 1s. Hence, 10s
  // should be sufficient here.
  constexpr auto kInputDurationSeconds = 10;

  // The output device, renderers, and capturer can each store exactly 1s of audio data.
  CreateOutput({{0xff, 0x00}}, kFormat, kPayloadFrames);
  auto renderer = CreateAudioRenderer(kFormat, kPayloadFrames);
  auto capturer = CreateAudioCapturer(kFormat, kPayloadFrames,
                                      fuchsia::media::AudioCapturerConfiguration::WithLoopback(
                                          fuchsia::media::LoopbackAudioCapturerConfiguration()));

  // The input buffer.
  //
  // This contains a repeated sequence generated from a wrapped int24_t counter.
  // The actual sample values are 32-bits, with the high 24 bits filled in and the
  // low 8 bits zero.
  //
  // The sequence length (not counting repetitions) cannot match the length of any
  // ring buffer inside audio_core. This ensures that audio_core won't reach a steady
  // state where it writes the same value to each field of the ring buffer -- that
  // would defeat the purpose of the test, which checks that ring buffer writes are
  // flushed before they are read by the capture path.
  //
  // The total length (including repetitions) should be longer than all ring buffers
  // inside audio_core, to ensure that audio_core's loopback buffer wraps around at
  // least once.

  // We prepend silence to our signal, to account for initial gain-ramping on Play.
  constexpr auto kNumInitialSilentFrames = kPacketFrames;
  auto silence = GenerateSilentAudio(kFormat, kNumInitialSilentFrames);
  auto silent_packets = renderer->AppendSlice(silence, kPacketFrames);

  AudioBuffer<ASF::SIGNED_24_IN_32> input(
      kFormat, kInputDurationSeconds * kFrameRate - kNumInitialSilentFrames);
  for (auto frame = 0; frame < input.NumFrames(); frame++) {
    input.samples()[frame] = frame << 8;
  }
  auto input_packets = renderer->AppendSlice(input, kPacketFrames, silence.NumFrames());

  // Collect all captured packets.
  std::vector<CapturedPacket> captured_packets;
  capturer->fidl().events().OnPacketProduced = [capturer, &captured_packets](StreamPacket p) {
    EXPECT_EQ(p.payload_buffer_id, 0u);
    captured_packets.push_back({
        .pts = p.pts,
        .data = capturer->SnapshotPacket(p),
    });
    capturer->fidl()->ReleasePacket(p);
  };
  capturer->fidl()->StartAsyncCapture(kPacketFrames);

  // Play inputs starting at `now + min_lead_time + tolerance`, where tolerance estimates
  // the maximum scheduling delay between reading the clock and the last call to Play.
  const auto tolerance = zx::msec(20);
  auto start_time = zx::clock::get_monotonic() + renderer->min_lead_time() + tolerance;
  renderer->Play(this, start_time, 0);

  // Wait until all packets are fully rendered (this includes any initial silent ones).
  renderer->WaitForPackets(this, input_packets);

  // Wait until we've captured a packet with pts > start_time + expected duration.
  // Note that PTS is relative to the capturer's clock, which defaults to the system
  // mono clock. We add an extra frame because in practice the actual start time might
  // be misaligned by a fractional frame.
  auto ns_per_frame = kFormat.frames_per_ns().Inverse();
  auto end_time = start_time + zx::nsec(ns_per_frame.Scale(input.NumFrames() + 1));

  RunLoopUntil([&captured_packets, end_time]() {
    return captured_packets.size() > 0 && captured_packets.back().pts > end_time.get();
  });

  // Stop the capturer so we don't overflow while doing the following checks.
  capturer->fidl().events().OnPacketProduced = nullptr;
  capturer->fidl()->StopAsyncCaptureNoReply();
  RunLoopUntilIdle();

  // Find the first output frame. Since input[0] == 0 (silence), look for input[1], which is 1.
  auto second_output_frame = FirstNonSilentFrame(captured_packets);
  ASSERT_FALSE(second_output_frame == std::nullopt)
      << "could not find data sample 0x1 in the captured output";

  auto [packet_it, frame] = *second_output_frame;
  if (frame == 0) {
    // In practice this should never fail, as capture starts before AudioCore emits any audio.
    FX_CHECK(packet_it > captured_packets.begin());
    packet_it--;
    frame = packet_it->data.NumFrames() - 1;
  } else {
    frame--;
  }

  // Gather the full captured audio into a buffer and compare vs the input.
  AudioBuffer<ASF::SIGNED_24_IN_32> capture_buffer(kFormat, 0);
  capture_buffer.samples().insert(capture_buffer.samples().end(),
                                  packet_it->data.samples().begin() + frame * kFormat.channels(),
                                  packet_it->data.samples().end());

  for (packet_it++; packet_it != captured_packets.end(); packet_it++) {
    capture_buffer.samples().insert(capture_buffer.samples().end(),
                                    packet_it->data.samples().begin(),
                                    packet_it->data.samples().end());
  }

  CompareAudioBufferOptions opts;
  opts.num_frames_per_packet = kPacketFrames;
  opts.test_label = "check data";
  CompareAudioBuffers(AudioBufferSlice(&capture_buffer, 0, input.NumFrames()),
                      AudioBufferSlice(&input), opts);
}

}  // namespace media::audio::test
