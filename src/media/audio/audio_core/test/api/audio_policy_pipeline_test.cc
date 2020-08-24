// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/device/audio.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"
#include "src/media/audio/lib/test/renderer_shim.h"

namespace media::audio::test {

constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
constexpr int32_t kSampleRate = 8000;
constexpr int kChannelCount = 1;
static const auto kFormat = Format::Create<kSampleFormat>(kChannelCount, kSampleRate).value();

constexpr int kRingBufferFrames = kSampleRate;  // 1s
static const int kRingBufferSamples = kRingBufferFrames * kFormat.channels();
static const int kRingBufferBytes = kRingBufferFrames * kFormat.bytes_per_frame();

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

  zx_duration_t GetMinLeadTime(std::initializer_list<AudioRendererShim<kSampleFormat>*> renderers);

  // Expect that the given packet contains nothing but the given samples.
  void ExpectPacketContains(std::string label, const fuchsia::media::StreamPacket& packet,
                            const AudioBuffer<kSampleFormat>& payload,
                            size_t expected_frames_per_packet, int16_t expected_data);
};

// AudioAdminTest implementation
//
void AudioAdminTest::SetUp() {
  HermeticAudioTest::SetUp();
  FailUponOverflowsOrUnderflows();
  SetUpVirtualAudioOutput();
  SetUpVirtualAudioInput();
}

// SetUpVirtualAudioOutput
//
// For loopback tests, setup the required audio output, using virtualaudio.
void AudioAdminTest::SetUpVirtualAudioOutput() {
  const audio_stream_unique_id_t kUniqueId{{0x4a, 0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a, 0x4a,
                                            0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a}};

  CreateOutput(kUniqueId, kFormat, kRingBufferFrames);
}

void AudioAdminTest::SetUpVirtualAudioInput() {
  const audio_stream_unique_id_t kUniqueId{{0x4a, 0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a, 0x4a,
                                            0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4b}};

  auto input = CreateInput(kUniqueId, kFormat, kRingBufferFrames);

  AudioBuffer buf(kFormat, kRingBufferFrames);
  for (size_t k = 0; k < buf.samples().size(); k++) {
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
  for (size_t k = 0; k < buf.samples().size(); k++) {
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

zx_duration_t AudioAdminTest::GetMinLeadTime(
    std::initializer_list<AudioRendererShim<kSampleFormat>*> renderers) {
  zx_duration_t max_min_lead_time = 0;
  for (auto renderer : renderers) {
    // Get the minimum duration after submitting a packet to when we can start
    // capturing what we sent on the loopback interface.  We use the longest
    // latency of any of the renderers, but they should all have the same value.
    max_min_lead_time = std::max(max_min_lead_time, renderer->GetMinLeadTime().get());
  }
  return max_min_lead_time;
}

void AudioAdminTest::ExpectPacketContains(std::string label,
                                          const fuchsia::media::StreamPacket& packet,
                                          const AudioBuffer<kSampleFormat>& payload,
                                          size_t expected_frames_per_packet,
                                          int16_t expected_data) {
  ASSERT_EQ(packet.payload_size, expected_frames_per_packet * kFormat.bytes_per_frame())
      << "unexpected frame count for packet " << label;

  for (size_t f = 0; f < expected_frames_per_packet; f++) {
    for (size_t c = 0; c < kFormat.channels(); c++) {
      size_t offset =
          (packet.payload_offset + f * kFormat.bytes_per_frame() + c * kFormat.bytes_per_sample()) %
          kRingBufferBytes;
      size_t sample = offset / kFormat.bytes_per_sample();
      ASSERT_EQ(fxl::StringPrintf("0x%x", payload.samples()[sample]),
                fxl::StringPrintf("0x%x", expected_data))
          << "unexpected value at sample[" << sample << "] for packet " << label;
    }
  }
}

// SingleRenderStream
//
// Creates a single output stream and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, SingleRenderStream) {
  // Setup a policy rule that MEDIA being active will not affect a BACKGROUND
  // capture.
  audio_core_->ResetInteractions();

  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::NONE);
  }

  fuchsia::media::StreamPacket packet, captured;

  // SetUp playback stream
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto capturer = SetUpLoopbackCapturer();

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = kRingBufferBytes;

  renderer->renderer()->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  renderer->renderer()->Play(zx::clock::get_monotonic().get(), 0,
                             AddCallback("Play", [&ref_time_received, &media_time_received](
                                                     int64_t ref_time, int64_t media_time) {
                               ref_time_received = ref_time;
                               media_time_received = media_time;
                             }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GE(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Add a callback for when we get our captured packet.
  capturer->capturer().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
        }
      });

  // Capture 10 frames of audio.
  capturer->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 frames as we expected.
  ExpectPacketContains("captured", captured, capturer->SnapshotPayload(), 10, kPlaybackData1);
}

// RenderMuteCapture
//
// Creates a single output stream and a loopback capture that is muted due to
// the output stream and verifies it gets silence on it.
TEST_F(AudioAdminTest, RenderMuteCapture) {
  // Setup a policy rule that MEDIA being active will mute a BACKGROUND
  // capture.
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::MUTE);
  }

  fuchsia::media::StreamPacket packet, captured;

  // SetUp playback stream
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT, kPlaybackData1);
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = kRingBufferBytes;

  renderer->renderer()->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  renderer->renderer()->Play(zx::clock::get_monotonic().get(), 0,
                             AddCallback("Play", [&ref_time_received, &media_time_received](
                                                     int64_t ref_time, int64_t media_time) {
                               ref_time_received = ref_time;
                               media_time_received = media_time;
                             }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GE(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  capturer->capturer().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
        }
      });
  capturer->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  ExpectPacketContains("captured", captured, capturer->SnapshotPayload(), 10, 0x0);
}

// CaptureMuteRender
//
// Creates a single output stream and a loopback capture and verifies that the
// Render stream is muted in the capturer.
TEST_F(AudioAdminTest, CaptureMuteRender) {
  // Setup a policy rule that MEDIA being active will mute a BACKGROUND
  // capture.
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_capture_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::MUTE);
  }

  // SetUp playback stream
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData1);
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);

  // Immediately start this capturer so that it impacts policy.
  capturer->capturer()->StartAsyncCapture(10);

  auto loopback_capturer = SetUpLoopbackCapturer();

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = kRingBufferBytes;
    renderer->renderer()->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  renderer->renderer()->Play(zx::clock::get_monotonic().get(), 0,
                             AddCallback("Play", [&ref_time_received, &media_time_received](
                                                     int64_t ref_time, int64_t media_time) {
                               ref_time_received = ref_time;
                               media_time_received = media_time;
                             }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GE(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Add a callback for when we get our captured packet.
  fuchsia::media::StreamPacket loopback_captured;
  bool produced_loopback_packet = false;
  loopback_capturer->capturer().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&loopback_captured, &produced_loopback_packet,
                                       ref_time_received](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (packet.pts > ref_time_received && loopback_captured.payload_size == 0) {
          loopback_captured = packet;
          produced_loopback_packet = true;
        }
      });

  // Capture 10 samples of audio.
  loopback_capturer->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  ExpectPacketContains("loopback_captured", loopback_captured, loopback_capturer->SnapshotPayload(),
                       10, 0x0);
}

// DualRenderStreamMix
//
// Creates a pair of output streams with different usages that the policy is to
// mix together, and a loopback capture and verifies it gets back what it puts
// in.
TEST_F(AudioAdminTest, DualRenderStreamMix) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::NONE);
  }
  // SetUp playback streams
  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData2);

  // SetUp loopback capture
  auto capturer = SetUpLoopbackCapturer();

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer1, renderer2});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (auto renderer : {renderer1, renderer2}) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = kRingBufferBytes;
    renderer->renderer()->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer1->renderer()->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  renderer2->renderer()->Play(playat, 0,
                              AddCallback("Play", [&ref_time_received, &media_time_received](
                                                      int64_t ref_time, int64_t media_time) {
                                ref_time_received = ref_time;
                                media_time_received = media_time;
                              }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GT(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  fuchsia::media::StreamPacket captured;
  capturer->capturer().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
        }
      });
  capturer->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  ExpectPacketContains("captured", captured, capturer->SnapshotPayload(), 10,
                       kPlaybackData1 + kPlaybackData2);
}

// DualRenderStreamDucking
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamDucking) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::INTERRUPTION);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::DUCK);
  }
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::INTERRUPTION);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::NONE);
  }
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::NONE);
  }
  // SetUp playback streams
  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::INTERRUPTION, kPlaybackData2);

  // SetUp loopback capture
  auto capturer = SetUpLoopbackCapturer();

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer1, renderer2});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (auto renderer : {renderer1, renderer2}) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = kRingBufferBytes;
    renderer->renderer()->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer1->renderer()->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  renderer2->renderer()->Play(playat, 0,
                              AddCallback("Play", [&ref_time_received, &media_time_received](
                                                      int64_t ref_time, int64_t media_time) {
                                ref_time_received = ref_time;
                                media_time_received = media_time;
                              }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GT(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  fuchsia::media::StreamPacket captured;
  capturer->capturer().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
        }
      });
  capturer->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  ExpectPacketContains("captured", captured, capturer->SnapshotPayload(), 10,
                       kDuckedPlaybackData1 + kPlaybackData2);
}

// DualRenderStreamMute
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamMute) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::MUTE);
  }

  // SetUp playback streams
  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData2);

  // SetUp loopback capture
  auto capturer = SetUpLoopbackCapturer();

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer1});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (auto renderer : {renderer1, renderer2}) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = kRingBufferBytes;
    renderer->renderer()->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer1->renderer()->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  renderer2->renderer()->Play(playat, 0,
                              AddCallback("Play", [&ref_time_received, &media_time_received](
                                                      int64_t ref_time, int64_t media_time) {
                                ref_time_received = ref_time;
                                media_time_received = media_time;
                              }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GT(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  fuchsia::media::StreamPacket captured;
  capturer->capturer().events().OnPacketProduced =
      AddCallback("OnPacketProduced", [&captured](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
        }
      });
  capturer->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  ExpectPacketContains("captured", captured, capturer->SnapshotPayload(), 10, kPlaybackData1);
}

// DualCaptureStreamNone
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams both remain unaffected.
TEST_F(AudioAdminTest, DualCaptureStreamNone) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::NONE);
  }

  // SetUp playback streams
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);

  // SetUp loopback capture
  auto capturer1 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  auto capturer2 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = kRingBufferBytes;
    renderer->renderer()->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer->renderer()->Play(playat, 0,
                             AddCallback("Play", [&ref_time_received, &media_time_received](
                                                     int64_t ref_time, int64_t media_time) {
                               ref_time_received = ref_time;
                               media_time_received = media_time;
                             }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GT(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  fuchsia::media::StreamPacket captured1;
  capturer1->capturer().events().OnPacketProduced =
      AddCallbackUnordered("OnPacketProduced", [&captured1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured1.payload_size == 0) {
          captured1 = packet;
        }
      });

  fuchsia::media::StreamPacket captured2;
  capturer2->capturer().events().OnPacketProduced =
      AddCallbackUnordered("OnPacketProduced", [&captured2](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured2.payload_size == 0) {
          captured2 = packet;
        }
      });

  capturer1->capturer()->StartAsyncCapture(10);
  capturer2->capturer()->StartAsyncCapture(10);
  ExpectCallback();

  // Check that all of the samples contain the expected data.
  ExpectPacketContains("captured1", captured1, capturer1->SnapshotPayload(), 10,
                       kVirtualInputSampleValue);
  ExpectPacketContains("captured2", captured2, capturer2->SnapshotPayload(), 10,
                       kVirtualInputSampleValue);
}

// DualCaptureStreamMute
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams of different usages can mute each other.
TEST_F(AudioAdminTest, DISABLED_DualCaptureStreamMute) {
  // Setup expected behavior from policy for this test
  audio_core_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_capture_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::MUTE);
  }

  // SetUp playback streams
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);

  // SetUp loopback capture
  auto capturer1 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
  auto capturer2 = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND);

  // Add a callback for when we get our captured packet.
  fuchsia::media::StreamPacket captured1;
  bool produced_packet1 = false;
  capturer1->capturer().events().OnPacketProduced = AddCallback(
      "OnPacketProduced", [&captured1, &produced_packet1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured1.payload_size == 0) {
          captured1 = packet;
          produced_packet1 = true;
        }
      });

  fuchsia::media::StreamPacket captured2;
  bool produced_packet2 = false;
  capturer2->capturer().events().OnPacketProduced = AddCallback(
      "OnPacketProduced", [&captured2, &produced_packet2](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured2.payload_size == 0) {
          captured2 = packet;
          produced_packet2 = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = kRingBufferBytes;
    renderer->renderer()->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer->renderer()->Play(playat, 0,
                             AddCallback("Play", [&ref_time_received, &media_time_received](
                                                     int64_t ref_time, int64_t media_time) {
                               ref_time_received = ref_time;
                               media_time_received = media_time;
                             }));
  ExpectCallback();

  // We expect that media_time 0 played back at some point after the 'zero'
  // time on the system.
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GT(ref_time_received, 0);

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  capturer1->capturer()->StartAsyncCapture(10);
  capturer2->capturer()->StartAsyncCapture(10);
  RunLoopUntil(
      [&produced_packet1, &produced_packet2]() { return produced_packet1 && produced_packet2; });

  // Check that we got 10 samples as we expected.
  ExpectPacketContains("captured1", captured1, capturer1->SnapshotPayload(), 10, kPlaybackData1);
  ExpectPacketContains("captured2", captured2, capturer2->SnapshotPayload(), 10, 0x0);
}

}  // namespace media::audio::test
