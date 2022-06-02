// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/device/audio.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/testing/integration/hermetic_audio_test.h"
#include "src/media/audio/audio_core/testing/integration/renderer_shim.h"

namespace media::audio::test {

namespace {
constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
constexpr int32_t kSampleRate = 8000;
constexpr int kChannelCount = 1;
const auto kFormat = Format::Create<kSampleFormat>(kChannelCount, kSampleRate).value();

constexpr int kRingBufferFrames = kSampleRate;  // 1s
const int kRingBufferSamples = kRingBufferFrames * kFormat.channels();
const int kRingBufferBytes = kRingBufferFrames * kFormat.bytes_per_frame();

// Extra delay in Play() calls to account for scheduling latency.
// This is intentionally set higher than likely necessary to reduce the chance of flakes.
constexpr auto kPlayLeadTimeTolerance = zx::msec(30);
}  // namespace

//
// AudioAdminTest
//
// Base Class for testing simple playback and capture with policy rules.
class AudioAdminTest : public HermeticAudioTest {
 protected:
  // We add this duration, in MS, to our lead time to make sure mixing has
  // completed.  5ms had a 0.5% failure rate when running in a loop.
  static const int kSampleDelayAddition = 5;

  static constexpr int16_t kInitialCaptureData = 0x7fff;
  static constexpr int16_t kPlaybackData1 = 0x1111;
  static constexpr int16_t kDuckedPlaybackData1 = 0x4e;  // reduced by 35dB
  static constexpr int16_t kPlaybackData2 = 0x2222;
  static constexpr int16_t kVirtualInputSampleValue = 0x3333;

  void SetUp() override;
  void TearDown() override;

  void SetUpVirtualAudioOutput();
  void SetUpVirtualAudioInput();

  AudioRendererShim<kSampleFormat>* SetUpRenderer(fuchsia::media::AudioRenderUsage usage,
                                                  int16_t data);

  AudioCapturerShim<kSampleFormat>* SetUpCapturer(
      fuchsia::media::AudioCapturerConfiguration configuration);
  AudioCapturerShim<kSampleFormat>* SetUpCapturer(fuchsia::media::AudioCaptureUsage usage) {
    fuchsia::media::InputAudioCapturerConfiguration input;
    input.set_usage(usage);
    return SetUpCapturer(fuchsia::media::AudioCapturerConfiguration::WithInput(std::move(input)));
  }
  AudioCapturerShim<kSampleFormat>* SetUpLoopbackCapturer() {
    return SetUpCapturer(fuchsia::media::AudioCapturerConfiguration::WithLoopback(
        fuchsia::media::LoopbackAudioCapturerConfiguration()));
  }

  std::pair<zx::time, zx::time> ComputePlayAndCaptureTimes(
      std::initializer_list<AudioRendererShim<kSampleFormat>*> renderers);

  // Expect that the given packet contains nothing but the given samples.
  void ExpectPacketContains(std::string label, const AudioBuffer<kSampleFormat>& packet,
                            int64_t expected_frames_per_packet, int16_t expected_data);

  void TestCaptureMuteRender(bool set_usage_to_disable);

  VirtualOutput<kSampleFormat>* output_ = nullptr;
};

// AudioAdminTest implementation
//
void AudioAdminTest::SetUp() {
  HermeticAudioTest::SetUp();
  SetUpVirtualAudioOutput();
  SetUpVirtualAudioInput();
}

void AudioAdminTest::TearDown() {
  if constexpr (kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    ExpectNoOverflowsOrUnderflows();
  } else {
    // We expect no renderer underflows: we pre-submit the whole signal. Keep that check enabled.
    ExpectNoRendererUnderflows();
  }

  HermeticAudioTest::TearDown();
}

// SetUpVirtualAudioOutput
//
// For loopback tests, setup the required audio output, using virtualaudio.
void AudioAdminTest::SetUpVirtualAudioOutput() {
  const audio_stream_unique_id_t kUniqueId{{0x4a, 0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a, 0x4a,
                                            0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a}};

  output_ = CreateOutput(kUniqueId, kFormat, kRingBufferFrames);
}

void AudioAdminTest::SetUpVirtualAudioInput() {
  const audio_stream_unique_id_t kUniqueId{{0x4a, 0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a, 0x4a,
                                            0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4b}};

  auto input = CreateInput(kUniqueId, kFormat, kRingBufferFrames);

  AudioBuffer buf(kFormat, kRingBufferFrames);
  for (auto k = 0u; k < buf.samples().size(); k++) {
    buf.samples()[k] = kVirtualInputSampleValue;
  }
  input->WriteRingBufferAt(0, &buf);
}

// SetUpRenderer
//
// For loopback tests, setup the first audio_renderer interface.
AudioRendererShim<kSampleFormat>* AudioAdminTest::SetUpRenderer(
    fuchsia::media::AudioRenderUsage usage, int16_t data) {
  auto r = CreateAudioRenderer(kFormat, kRingBufferFrames, usage);

  AudioBuffer buf(kFormat, kRingBufferFrames);
  for (auto k = 0u; k < buf.samples().size(); k++) {
    buf.samples()[k] = data;
  }
  r->payload().Append(AudioBufferSlice(&buf));
  return r;
}

// SetUpCapturer
//
// For loopback tests, setup an audio_capturer interface
AudioCapturerShim<kSampleFormat>* AudioAdminTest::SetUpCapturer(
    fuchsia::media::AudioCapturerConfiguration configuration) {
  auto c = CreateAudioCapturer(kFormat, kRingBufferFrames, std::move(configuration));
  c->payload().Memset<kSampleFormat>(kInitialCaptureData);
  return c;
}

std::pair<zx::time, zx::time> AudioAdminTest::ComputePlayAndCaptureTimes(
    std::initializer_list<AudioRendererShim<kSampleFormat>*> renderers) {
  // Compute the largest lead time for all renderers.
  zx::duration lead_time;
  for (auto renderer : renderers) {
    // Get the minimum duration after submitting a packet to when we can start
    // capturing what we sent on the loopback interface.  We use the longest
    // latency of any of the renderers, but they should all have the same value.
    lead_time = std::max(lead_time, renderer->min_lead_time());
  }

  // The play time is now + lead time + some tolerance to account for the delay between now
  // and when the Play call actually runs inside audio_core.
  auto play_time = zx::clock::get_monotonic() + lead_time + kPlayLeadTimeTolerance;

  // We can start capturing after the output pipeline has completed one mix job (10ms by default).
  // Our renderer's playback buffers contain the same sample repeated over-and-over for a long
  // period of time (much longer than one mix job) so as long as the capturer wakes up shortly after
  // this capture_time, there should be plenty of time to capture the rendered audio.
  auto capture_time = play_time + zx::msec(10);

  return std::make_pair(play_time, capture_time);
}

void AudioAdminTest::ExpectPacketContains(std::string label,
                                          const AudioBuffer<kSampleFormat>& packet,
                                          int64_t expected_frames_per_packet,
                                          int16_t expected_data) {
  ASSERT_EQ(packet.NumFrames(), expected_frames_per_packet)
      << "unexpected frame count for packet " << label;

  for (int64_t f = 0; f < expected_frames_per_packet; f++) {
    for (int32_t c = 0; c < kFormat.channels(); c++) {
      ASSERT_EQ(fxl::StringPrintf("0x%x", packet.SampleAt(f, c)),
                fxl::StringPrintf("0x%x", expected_data))
          << "unexpected value at sample[frame=" << f << ",chan=" << c << "] for packet " << label;
    }
  }
}

// SingleRenderStream
//
// Creates a single output stream and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, SingleRenderStream) {
  // Setup a policy rule that MEDIA being active will not affect a BACKGROUND capture.
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND),
      fuchsia::media::Behavior::NONE);

  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto capturer = SetUpLoopbackCapturer();

  renderer->fidl()->SendPacketNoReply({
      .payload_offset = 0,
      .payload_size = static_cast<uint64_t>(kRingBufferBytes),
  });

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer});
  renderer->fidl()->Play(play_time.get(), 0,
                         AddCallback("Play", [](int64_t ref_time, int64_t media_time) {
                           EXPECT_EQ(media_time, 0);
                           EXPECT_GE(ref_time, 0);
                         }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 frames of audio.
  std::optional<AudioBuffer<kSampleFormat>> captured;
  capturer->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured, capturer](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured == std::nullopt) {
          captured = capturer->SnapshotPacket(packet);
        }
      });

  capturer->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that we got 10 frames containing the exact data values we expected.
  ASSERT_NE(captured, std::nullopt);
  ExpectPacketContains("captured", *captured, 10, kPlaybackData1);
}

// RenderMuteCapture
//
// Creates a single output stream and a loopback capture that is muted due to
// the output stream and verifies it gets silence on it.
TEST_F(AudioAdminTest, RenderMuteCapture) {
  // Setup a policy rule that MEDIA being active will mute a BACKGROUND capture.
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND),
      fuchsia::media::Behavior::MUTE);

  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT, kPlaybackData1);
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);

  renderer->fidl()->SendPacketNoReply({
      .payload_offset = 0,
      .payload_size = static_cast<uint64_t>(kRingBufferBytes),
  });

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer});
  renderer->fidl()->Play(play_time.get(), 0,
                         AddCallback("Play", [](int64_t ref_time, int64_t media_time) {
                           EXPECT_EQ(media_time, 0);
                           EXPECT_GE(ref_time, 0);
                         }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 samples of audio.
  std::optional<AudioBuffer<kSampleFormat>> captured;
  capturer->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured, capturer](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured == std::nullopt) {
          captured = capturer->SnapshotPacket(packet);
        }
      });
  capturer->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that we got 10 frames containing the exact data values we expected.
  ASSERT_NE(captured, std::nullopt);
  ExpectPacketContains("captured", *captured, 10, 0x0);
}

// CaptureMuteRender
//
// Creates a single output stream and a capture stream and verifies that the
// render stream is muted when the capturer is enabled.
//
// If set_usage_to_disable=true, then after starting the capturer, we immediately
// change the capturer's usage, which should unmute the render stream.
void AudioAdminTest::TestCaptureMuteRender(bool set_usage_to_disable) {
  // Setup a policy rule that MEDIA being active will mute a BACKGROUND capture.
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::BACKGROUND),
      fuchsia::media::Behavior::MUTE);

  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData1);
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
  auto loopback_capturer = SetUpLoopbackCapturer();

  // Immediately start this capturer so that it impacts policy.
  capturer->fidl()->StartAsyncCapture(10);
  if (set_usage_to_disable) {
    capturer->fidl()->SetUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  }

  renderer->fidl()->SendPacketNoReply({
      .payload_offset = 0,
      .payload_size = static_cast<uint64_t>(kRingBufferBytes),
  });

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer});
  renderer->fidl()->Play(play_time.get(), 0,
                         AddCallback("Play", [](int64_t ref_time, int64_t media_time) {
                           EXPECT_EQ(media_time, 0);
                           EXPECT_GE(ref_time, 0);
                         }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Add a callback for when we get our captured packet.
  std::optional<AudioBuffer<kSampleFormat>> loopback_captured;
  loopback_capturer->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced",
                  [&loopback_captured, loopback_capturer](fuchsia::media::StreamPacket packet) {
                    // We only care about the first set of loopback_captured samples
                    if (loopback_captured == std::nullopt) {
                      loopback_captured = loopback_capturer->SnapshotPacket(packet);
                    }
                  });

  // Capture 10 samples of audio.
  loopback_capturer->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that we got 10 frames containing the exact data values we expected.
  int16_t expected_data = set_usage_to_disable ? kPlaybackData1 : 0x0;
  ExpectPacketContains("loopback_captured", *loopback_captured, 10, expected_data);
}

TEST_F(AudioAdminTest, CaptureMuteRender) { TestCaptureMuteRender(false); }

TEST_F(AudioAdminTest, CaptureDoesntMuteRenderAfterSetUsage) { TestCaptureMuteRender(true); }

// DualRenderStreamMix
//
// Creates a pair of output streams with different usages that the policy is to
// mix together, and a loopback capture and verifies it gets back what it puts
// in.
TEST_F(AudioAdminTest, DualRenderStreamMix) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND),
      fuchsia::media::Behavior::NONE);

  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData2);
  auto capturer = SetUpLoopbackCapturer();

  for (auto renderer : {renderer1, renderer2}) {
    renderer->fidl()->SendPacketNoReply({
        .payload_offset = 0,
        .payload_size = static_cast<uint64_t>(kRingBufferBytes),
    });
  }

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer1, renderer2});
  renderer1->fidl()->Play(play_time.get(), 0,
                          AddCallback("Play1", [](int64_t ref_time, int64_t media_time) {
                            EXPECT_EQ(media_time, 0);
                            EXPECT_GE(ref_time, 0);
                          }));
  renderer2->fidl()->Play(play_time.get(), 0,
                          AddCallback("Play2", [](int64_t ref_time, int64_t media_time) {
                            EXPECT_EQ(media_time, 0);
                            EXPECT_GE(ref_time, 0);
                          }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 samples of audio.
  std::optional<AudioBuffer<kSampleFormat>> captured;
  capturer->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured, capturer](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured == std::nullopt) {
          captured = capturer->SnapshotPacket(packet);
        }
      });

  capturer->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that we got 10 frames containing the exact data values we expected.
  ASSERT_NE(captured, std::nullopt);
  ExpectPacketContains("captured", *captured, 10, kPlaybackData1 + kPlaybackData2);
}

// DualRenderStreamDucking
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamDucking) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Behavior::DUCK);

  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::BACKGROUND),
      fuchsia::media::Behavior::NONE);

  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::BACKGROUND),
      fuchsia::media::Behavior::NONE);

  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::INTERRUPTION, kPlaybackData2);
  auto capturer = SetUpLoopbackCapturer();

  for (auto renderer : {renderer1, renderer2}) {
    renderer->fidl()->SendPacketNoReply({
        .payload_offset = 0,
        .payload_size = static_cast<uint64_t>(kRingBufferBytes),
    });
  }

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer1, renderer2});
  renderer1->fidl()->Play(play_time.get(), 0,
                          AddCallback("Play1", [](int64_t ref_time, int64_t media_time) {
                            EXPECT_EQ(media_time, 0);
                            EXPECT_GE(ref_time, 0);
                          }));
  renderer2->fidl()->Play(play_time.get(), 0,
                          AddCallback("Play2", [](int64_t ref_time, int64_t media_time) {
                            EXPECT_EQ(media_time, 0);
                            EXPECT_GE(ref_time, 0);
                          }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 samples of audio.
  std::optional<AudioBuffer<kSampleFormat>> captured;
  capturer->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured, capturer](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured == std::nullopt) {
          captured = capturer->SnapshotPacket(packet);
        }
      });

  capturer->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that we got 10 frames containing the exact data values we expected.
  ASSERT_NE(captured, std::nullopt);
  ExpectPacketContains("captured", *captured, 10, kDuckedPlaybackData1 + kPlaybackData2);
}

// DualRenderStreamMute
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamMute) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::BACKGROUND),
      fuchsia::media::Behavior::MUTE);

  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData2);
  auto capturer = SetUpLoopbackCapturer();

  for (auto renderer : {renderer1, renderer2}) {
    renderer->fidl()->SendPacketNoReply({
        .payload_offset = 0,
        .payload_size = static_cast<uint64_t>(kRingBufferBytes),
    });
  }

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer1, renderer2});
  renderer1->fidl()->Play(play_time.get(), 0,
                          AddCallback("Play1", [](int64_t ref_time, int64_t media_time) {
                            EXPECT_EQ(media_time, 0);
                            EXPECT_GE(ref_time, 0);
                          }));
  renderer2->fidl()->Play(play_time.get(), 0,
                          AddCallback("Play2", [](int64_t ref_time, int64_t media_time) {
                            EXPECT_EQ(media_time, 0);
                            EXPECT_GE(ref_time, 0);
                          }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 samples of audio.
  std::optional<AudioBuffer<kSampleFormat>> captured;
  capturer->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured, capturer](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured == std::nullopt) {
          captured = capturer->SnapshotPacket(packet);
        }
      });

  capturer->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that we got 10 frames containing the exact data values we expected.
  ASSERT_NE(captured, std::nullopt);
  ExpectPacketContains("captured", *captured, 10, kPlaybackData1);
}

// DualCaptureStreamNone
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams both remain unaffected.
TEST_F(AudioAdminTest, DualCaptureStreamNone) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND),
      fuchsia::media::Behavior::NONE);

  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto capturer1 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  auto capturer2 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);

  renderer->fidl()->SendPacketNoReply({
      .payload_offset = 0,
      .payload_size = static_cast<uint64_t>(kRingBufferBytes),
  });

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer});
  renderer->fidl()->Play(play_time.get(), 0,
                         AddCallback("Play", [](int64_t ref_time, int64_t media_time) {
                           EXPECT_EQ(media_time, 0);
                           EXPECT_GE(ref_time, 0);
                         }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 samples of audio.
  std::optional<AudioBuffer<kSampleFormat>> captured1;
  capturer1->fidl().events().OnPacketProduced = AddCallbackUnordered(
      "OnPacketProduced", [&captured1, capturer1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured1 == std::nullopt) {
          captured1 = capturer1->SnapshotPacket(packet);
        }
      });

  std::optional<AudioBuffer<kSampleFormat>> captured2;
  capturer2->fidl().events().OnPacketProduced = AddCallbackUnordered(
      "OnPacketProduced", [&captured2, capturer2](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured2 == std::nullopt) {
          captured2 = capturer2->SnapshotPacket(packet);
        }
      });

  capturer1->fidl()->StartAsyncCapture(10);
  capturer2->fidl()->StartAsyncCapture(10);
  ExpectCallbacks();

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that all the frames contained the exact data values we expected.
  ASSERT_NE(captured1, std::nullopt);
  ASSERT_NE(captured2, std::nullopt);
  ExpectPacketContains("captured1", *captured1, 10, kVirtualInputSampleValue);
  ExpectPacketContains("captured2", *captured2, 10, kVirtualInputSampleValue);
}

// DualCaptureStreamMute
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams of different usages can mute each other.
TEST_F(AudioAdminTest, DISABLED_DualCaptureStreamMute) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  audio_core_->SetInteraction(
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT),
      fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND),
      fuchsia::media::Behavior::MUTE);

  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto capturer1 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
  auto capturer2 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);

  // Add a callback for when we get our captured packet.
  std::optional<AudioBuffer<kSampleFormat>> captured1;
  capturer1->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured1, capturer1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured1 == std::nullopt) {
          captured1 = capturer1->SnapshotPacket(packet);
        }
      });

  std::optional<AudioBuffer<kSampleFormat>> captured2;
  capturer2->fidl().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured2, capturer2](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured2 == std::nullopt) {
          captured2 = capturer2->SnapshotPacket(packet);
        }
      });

  renderer->fidl()->SendPacketNoReply({
      .payload_offset = 0,
      .payload_size = static_cast<uint64_t>(kRingBufferBytes),
  });

  // Start rendering.
  auto [play_time, capture_time] = ComputePlayAndCaptureTimes({renderer});
  renderer->fidl()->Play(play_time.get(), 0,
                         AddCallback("Play", [](int64_t ref_time, int64_t media_time) {
                           EXPECT_EQ(media_time, 0);
                           EXPECT_GE(ref_time, 0);
                         }));
  ExpectCallbacks();

  // Give the playback some time to get mixed.
  zx_nanosleep(capture_time.get());

  // Capture 10 samples of audio.
  capturer1->fidl()->StartAsyncCapture(10);
  capturer2->fidl()->StartAsyncCapture(10);
  RunLoopUntil([&captured1, &captured2]() { return captured1 && captured2; });

  if constexpr (!kEnableAllOverflowAndUnderflowChecksInRealtimeTests) {
    // In case of underflows, exit NOW (don't assess this buffer).
    // TODO(fxbug.dev/80003): Remove workarounds when underflow conditions are fixed.
    if (DeviceHasUnderflows(output_)) {
      GTEST_SKIP() << "Skipping data checks due to underflows";
      __builtin_unreachable();
    }
  }

  // Check that all the frames contained the exact data values we expected.
  ASSERT_NE(captured1, std::nullopt);
  ASSERT_NE(captured2, std::nullopt);
  ExpectPacketContains("captured1", *captured1, 10, kPlaybackData1);
  ExpectPacketContains("captured2", *captured2, 10, 0x0);
}

}  // namespace media::audio::test
