// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include "lib/media/audio/cpp/types.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// Although these tests don't look at packet data, they look at timestamps and rely on
// deadline scheduling, hence this test must be executed on real hardware.
class AudioCapturerReleaseTest : public HermeticAudioTest {
 protected:
  // VMO size is rounded up to the nearest multiple of 4096. These numbers are
  // designed so that kNumPackets * kFramesPerPacket rounds up to 4096.
  const size_t kNumPackets = 5;
  const size_t kFramesPerPacket = (4096 / sizeof(int16_t)) / kNumPackets;
  const size_t kBytesPerPacket = kFramesPerPacket * sizeof(int16_t);
  const size_t kFrameRate = 8000;
  const zx::duration kPacketDuration = zx::nsec(1'000'000'000 * kFramesPerPacket / kFrameRate);

  void SetUp() {
    HermeticAudioTest::SetUp();

    audio_core_->CreateAudioCapturer(false, audio_capturer_.NewRequest());
    AddErrorHandler(audio_capturer_, "AudioCapturer");

    auto t =
        media::CreateAudioStreamType(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, kFrameRate);
    format_ = Format::Create(t).take_value();
    audio_capturer_->SetPcmStreamType(t);

    auto num_frames = kNumPackets * kFramesPerPacket;
    zx::vmo audio_capturer_vmo;
    auto status = zx::vmo::create(num_frames * sizeof(int16_t), 0, &audio_capturer_vmo);
    ASSERT_EQ(status, ZX_OK) << "Failed to create payload buffer";
    audio_capturer_->AddPayloadBuffer(0, std::move(audio_capturer_vmo));
  }

  std::optional<Format> format_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::audio::GainControlPtr gain_control_;
};

// TODO(fxbug.dev/43507): Remove this test.
TEST_F(AudioCapturerReleaseTest, AsyncCapture_PacketsAutoReleased) {
  zx::time start_pts;
  size_t count = 0;
  audio_capturer_.events().OnPacketProduced = [this, &count,
                                               &start_pts](fuchsia::media::StreamPacket p) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", count));

    // Check that we're receiving the expected packets.
    auto pts = zx::time(p.pts);
    if (count == 0) {
      start_pts = zx::time(p.pts);
    } else {
      auto got = pts - start_pts;
      auto want = kPacketDuration * count;
      EXPECT_LT(std::abs(got.get() - want.get()), zx::usec(100).get())
          << "\n  expected time: " << want.get() << "\n       got time: " << got.get();
    }

    EXPECT_EQ(p.payload_buffer_id, 0u);
    EXPECT_EQ(p.payload_offset, (count % kNumPackets) * kBytesPerPacket);
    EXPECT_EQ(p.payload_size, kBytesPerPacket);
    EXPECT_EQ((count == 0), (p.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) != 0)
        << "\nflags: " << std::hex << p.flags;
    count++;
  };

  audio_capturer_->StartAsyncCapture(kFramesPerPacket);

  // To verify that we're automatically recycling packets, we need to loop
  // through the payload buffer at least twice.
  const zx::duration kLoopTimeout = zx::sec(10);
  RunLoopWithTimeoutOrUntil([this, &count]() { return ErrorOccurred() || count > 2 * kNumPackets; },
                            kLoopTimeout);

  ASSERT_FALSE(ErrorOccurred());
  ASSERT_GT(count, 2 * kNumPackets);
}

// TODO(fxbug.dev/43507): This will become the default behavior.
class AudioCapturerReleaseNewBehaviorTest : public AudioCapturerReleaseTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_arguments = {"--captures-must-release-packets"},
    });
  }
};

TEST_F(AudioCapturerReleaseNewBehaviorTest, AsyncCapture_PacketsManuallyReleased) {
  zx::time start_pts;
  size_t count = 0;
  audio_capturer_.events().OnPacketProduced = [this, &count,
                                               &start_pts](fuchsia::media::StreamPacket p) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", count));

    // Check that we're receiving the expected packets.
    auto pts = zx::time(p.pts);
    if (count == 0) {
      start_pts = zx::time(p.pts);
    } else {
      auto got = pts - start_pts;
      auto want = kPacketDuration * count;
      EXPECT_LT(std::abs(got.get() - want.get()), zx::msec(1).get())
          << "\n  expected time: " << want.get() << "\n       got time: " << got.get();
    }

    EXPECT_EQ(p.payload_buffer_id, 0u);
    EXPECT_EQ(p.payload_offset, (count % kNumPackets) * kBytesPerPacket);
    EXPECT_EQ(p.payload_size, kBytesPerPacket);
    EXPECT_EQ((count == 0), (p.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) != 0)
        << "\nflags: " << std::hex << p.flags;
    count++;

    // Manually release.
    audio_capturer_->ReleasePacket(p);
  };

  audio_capturer_->StartAsyncCapture(kFramesPerPacket);

  // To verify that we're automatically recycling packets, we need to loop
  // through the payload buffer at least twice.
  const zx::duration kLoopTimeout = zx::sec(10);
  RunLoopWithTimeoutOrUntil([this, &count]() { return ErrorOccurred() || count > 2 * kNumPackets; },
                            kLoopTimeout);

  ASSERT_FALSE(ErrorOccurred());
  ASSERT_GT(count, 2 * kNumPackets);
}

TEST_F(AudioCapturerReleaseNewBehaviorTest, AsyncCapture_PacketsNotManuallyReleased) {
  std::vector<fuchsia::media::StreamPacket> packets;

  // Do NOT manually release any packets.
  zx::time start_pts;
  size_t count = 0;
  audio_capturer_.events().OnPacketProduced = [this, &count, &start_pts,
                                               &packets](fuchsia::media::StreamPacket p) {
    SCOPED_TRACE(fxl::StringPrintf("packet %lu", count));

    // Check that we're receiving the expected packets.
    auto pts = zx::time(p.pts);
    if (count == 0) {
      start_pts = zx::time(p.pts);
    } else {
      auto got = pts - start_pts;
      auto want = kPacketDuration * count;
      EXPECT_LT(std::abs(got.get() - want.get()), zx::msec(1).get())
          << "\n  expected time: " << want.get() << "\n       got time: " << got.get();
    }

    EXPECT_EQ(p.payload_buffer_id, 0u);
    EXPECT_EQ(p.payload_offset, (count % kNumPackets) * kBytesPerPacket);
    EXPECT_EQ(p.payload_size, kBytesPerPacket);
    EXPECT_EQ((count == 0), (p.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) != 0)
        << "\nflags: " << std::hex << p.flags;
    count++;

    // Save so we can release these later.
    packets.push_back(p);
  };

  audio_capturer_->StartAsyncCapture(kFramesPerPacket);

  // We expect exactly kNumPackets.
  const zx::duration kLoopTimeout = zx::sec(10);
  RunLoopWithTimeoutOrUntil([this, &count]() { return ErrorOccurred() || count >= kNumPackets; },
                            kLoopTimeout);

  // To verify that we don't get additional packets, wait for the duration
  // of one more loop through the payload buffer.
  RunLoopWithTimeoutOrUntil([this]() { return ErrorOccurred(); }, kPacketDuration * kNumPackets);

  ASSERT_FALSE(ErrorOccurred());
  ASSERT_EQ(count, kNumPackets);

  // After releasing all packets, we should get at least one more packet.
  // This packet has a discontinuous timestamp.
  count = 0;
  audio_capturer_.events().OnPacketProduced = [this, &count,
                                               &start_pts](fuchsia::media::StreamPacket p) {
    SCOPED_TRACE(fxl::StringPrintf("after release, packet %lu", count));

    // All further packets should be some time after the endpoint of the last released packet.
    auto pts = zx::time(p.pts) - start_pts;
    auto last_end_pts = kPacketDuration * kNumPackets;
    EXPECT_GT(pts.get(), last_end_pts.get());
    EXPECT_EQ(p.payload_buffer_id, 0u);
    EXPECT_EQ(p.payload_size, kBytesPerPacket);
    EXPECT_EQ((count == 0), (p.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) != 0)
        << "\nflags: " << std::hex << p.flags;
    count++;
  };

  for (auto& p : packets) {
    audio_capturer_->ReleasePacket(p);
  }
  RunLoopWithTimeoutOrUntil([this, &count]() { return ErrorOccurred() || count > 0; },
                            kLoopTimeout);
  ASSERT_FALSE(ErrorOccurred());
  ASSERT_GT(count, 0u);
}

}  // namespace media::audio::test
