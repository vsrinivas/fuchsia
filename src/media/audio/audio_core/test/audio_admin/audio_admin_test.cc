// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/clock.h>
#include <zircon/device/audio.h>

#include <fbl/algorithm.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

//
// AudioAdminTest
//
// Base Class for testing simple playback and capture with policy rules.
class AudioAdminTest : public HermeticAudioTest {
 protected:
  static const int32_t kSampleRate = 8000;
  static const int kChannelCount = 1;
  static const int kSampleSeconds = 1;

  // We add this duration, in MS, to our lead time to make sure mixing has
  // completed.  5ms had a 0.5% failure rate when running in a loop.
  static const int kSampleDelayAddition = 5;

  static constexpr int16_t kInitialCaptureData = 0x7fff;
  static constexpr int16_t kPlaybackData1 = 0x1111;
  static constexpr int16_t kDuckedPlaybackData1 = 0x0368;  // reduced by 14dB
  static constexpr int16_t kPlaybackData2 = 0x2222;

  static void SetUpTestSuite();
  static void TearDownTestSuite();

  void SetUp() override;
  void TearDown() override;

  void SetUpVirtualAudioOutput();

  void SetUpRenderer(unsigned int index, fuchsia::media::AudioRenderUsage usage, int16_t data);
  void CleanUpRenderer(unsigned int index);

  void SetUpCapturer(unsigned int index, fuchsia::media::AudioCaptureUsage usage, int16_t data);

  zx_duration_t GetMinLeadTime(size_t num_renderers);

  fuchsia::media::AudioRendererPtr audio_renderer_[2];
  fzl::VmoMapper payload_buffer_[2];
  size_t playback_size_[2];
  size_t playback_sample_size_[2];

  fuchsia::media::AudioCapturerPtr audio_capturer_[2];
  fzl::VmoMapper capture_buffer_[2];
  size_t capture_size_[2];
  size_t capture_sample_size_[2];

  fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum_;
  uint64_t virtual_audio_output_token_;

  fuchsia::media::AudioCoreSyncPtr audio_core_sync_;
  static fuchsia::virtualaudio::ControlSyncPtr virtual_audio_control_sync_;
  fuchsia::virtualaudio::OutputSyncPtr virtual_audio_output_sync_;
};

fuchsia::virtualaudio::ControlSyncPtr AudioAdminTest::virtual_audio_control_sync_;

// static
void AudioAdminTest::SetUpTestSuite() {
  HermeticAudioTest::SetUpTestSuite();

  // Ensure that virtualaudio is enabled, before testing commences.
  environment()->ConnectToService(virtual_audio_control_sync_.NewRequest());
  ASSERT_EQ(ZX_OK, virtual_audio_control_sync_->Enable());
}

// static
void AudioAdminTest::TearDownTestSuite() {
  // Ensure that virtualaudio is disabled, by the time we leave.
  ASSERT_EQ(ZX_OK, virtual_audio_control_sync_->Disable());

  HermeticAudioTest::TearDownTestSuite();
}

// AudioAdminTest implementation
//
void AudioAdminTest::SetUp() {
  HermeticAudioTest::SetUp();

  environment()->ConnectToService(audio_dev_enum_.NewRequest());
  audio_dev_enum_.set_error_handler(ErrorHandler());

  SetUpVirtualAudioOutput();

  audio_dev_enum_.events().OnDeviceAdded =
      CompletionCallback([](fuchsia::media::AudioDeviceInfo unused) {
        FAIL() << "Audio device added while test was running";
      });

  audio_dev_enum_.events().OnDeviceRemoved = CompletionCallback([this](uint64_t token) {
    ASSERT_NE(token, virtual_audio_output_token_) << "Audio device removed while test was running";
  });

  audio_dev_enum_.events().OnDefaultDeviceChanged =
      CompletionCallback([](uint64_t old_default_token, uint64_t new_default_token) {
        FAIL() << "Default route changed while test was running.";
      });

  environment()->ConnectToService(audio_core_sync_.NewRequest());
  audio_core_sync_->SetSystemGain(0.0f);
  audio_core_sync_->SetSystemMute(false);
}

void AudioAdminTest::TearDown() {
  for (auto& capturer : audio_capturer_) {
    capturer.Unbind();
  }
  for (auto& renderer : audio_renderer_) {
    renderer.Unbind();
  }

  bool removed = false;
  audio_dev_enum_.events().OnDeviceRemoved =
      CompletionCallback([&removed, want_token = virtual_audio_output_token_](uint64_t token) {
        if (token == want_token) {
          removed = true;
        }
      });
  audio_dev_enum_.events().OnDeviceAdded = nullptr;
  audio_dev_enum_.events().OnDefaultDeviceChanged = nullptr;

  // Remove our virtual audio output device
  if (virtual_audio_output_sync_.is_bound()) {
    zx_status_t status = virtual_audio_output_sync_->Remove();
    ASSERT_EQ(status, ZX_OK) << "Failed to remove virtual audio output";

    virtual_audio_output_sync_.Unbind();
  }

  RunLoopUntil([&removed]() { return removed; });

  EXPECT_TRUE(audio_dev_enum_.is_bound());
  EXPECT_TRUE(audio_core_sync_.is_bound());

  HermeticAudioTest::TearDown();
}

// SetUpVirtualAudioOutput
//
// For loopback tests, setup the required audio output, using virtualaudio.
void AudioAdminTest::SetUpVirtualAudioOutput() {
  std::string dev_uuid_read = "4a41494a4a41494a4a41494a4a41494a";
  std::array<uint8_t, 16> dev_uuid{0x4a, 0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a,
                                   0x4a, 0x41, 0x49, 0x4a, 0x4a, 0x41, 0x49, 0x4a};

  // Connect to audio device enumerator to handle device topology changes during
  // test execution.
  audio_dev_enum_.events().OnDeviceAdded = [this,
                                            dev_uuid_read](fuchsia::media::AudioDeviceInfo dev) {
    if (dev.unique_id == dev_uuid_read) {
      virtual_audio_output_token_ = dev.token_id;
    }
  };

  uint64_t default_dev = 0;
  audio_dev_enum_.events().OnDefaultDeviceChanged = [&default_dev](uint64_t old_default_token,
                                                                   uint64_t new_default_token) {
    default_dev = new_default_token;
  };

  // Ensure that that our connection to the device enumerator has completed
  // enumerating the audio devices if any exist before we add ours.  This serves
  // as a synchronization point to make sure audio_core has our OnDeviceAdded
  // and OnDefaultDeviceChanged callbacks registered before we trigger the
  // device add.  Without this call, the add for the virtual output may be
  // picked up and processed in the device_manager in audio_core before it's
  // added our listener for events.
  audio_dev_enum_->GetDevices(
      CompletionCallback([](std::vector<fuchsia::media::AudioDeviceInfo> devices) {}));
  ExpectCallback();

  // Loopback capture requires an active audio output. Use virtualaudio to add a virtual output.
  EXPECT_FALSE(virtual_audio_output_sync_.is_bound());
  environment()->ConnectToService(virtual_audio_output_sync_.NewRequest());

  // Create an output device using default settings, save it while tests run.
  auto status = virtual_audio_output_sync_->SetUniqueId(dev_uuid);
  ASSERT_EQ(status, ZX_OK) << "Failed to set virtual audio output uuid";

  // We want to set the virtual audio output to exactly the same format as we are sending and
  // receiving, to minimize any potential change in data. Each virtual audio device has one format
  // range by default, so we must first remove that, before then adding the format range we need.
  status = virtual_audio_output_sync_->ClearFormatRanges();
  ASSERT_EQ(status, ZX_OK) << "Failed to clear preexisting virtual audio output format ranges";

  status = virtual_audio_output_sync_->AddFormatRange(AUDIO_SAMPLE_FORMAT_16BIT, kSampleRate,
                                                      kSampleRate, kChannelCount, kChannelCount,
                                                      ASF_RANGE_FLAG_FPS_CONTINUOUS);
  ASSERT_EQ(status, ZX_OK) << "Failed to add virtual audio output format range";

  status = virtual_audio_output_sync_->Add();
  ASSERT_EQ(status, ZX_OK) << "Failed to add virtual audio output";

  // Wait for OnDeviceAdded and OnDefaultDeviceChanged callback.  These will
  // both need to have happened for the new device to be used for the test.
  RunLoopUntil([this, &default_dev]() {
    return virtual_audio_output_token_ != 0 && default_dev == virtual_audio_output_token_;
  });

  ASSERT_EQ(virtual_audio_output_token_, default_dev)
      << "Timed out waiting for audio_core to make the virtual audio output "
         "the default.";
}

// SetUpRenderer
//
// For loopback tests, setup the first audio_renderer interface.
void AudioAdminTest::SetUpRenderer(unsigned int index, fuchsia::media::AudioRenderUsage usage,
                                   int16_t data) {
  FX_CHECK(index < fbl::count_of(audio_renderer_)) << "Renderer index too high";

  int16_t* buffer;
  zx::vmo payload_vmo;

  audio_core_sync_->CreateAudioRenderer(audio_renderer_[index].NewRequest());

  audio_renderer_[index].set_error_handler(ErrorHandler());

  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format.channels = kChannelCount;
  format.frames_per_second = kSampleRate;

  playback_sample_size_[index] = sizeof(int16_t);

  playback_size_[index] =
      format.frames_per_second * format.channels * playback_sample_size_[index] * kSampleSeconds;

  zx_status_t status = payload_buffer_[index].CreateAndMap(
      playback_size_[index], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &payload_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Renderer VmoMapper:::CreateAndMap(" << index << ") failed - "
                           << status;

  buffer = reinterpret_cast<int16_t*>(payload_buffer_[index].start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++) {
    buffer[i] = data;
  }

  audio_renderer_[index]->SetUsage(usage);
  audio_renderer_[index]->SetPcmStreamType(format);
  audio_renderer_[index]->AddPayloadBuffer(0, std::move(payload_vmo));

  // All audio renderers, by default, are set to 0 dB unity gain (passthru).
}

// CleanUpRenderer
//
// Flush the output and free the vmo that was used by Renderer1.
void AudioAdminTest::CleanUpRenderer(unsigned int index) {
  FX_CHECK(index < fbl::count_of(audio_renderer_)) << "Renderer index too high";

  payload_buffer_[index].Unmap();
}

// SetUpCapturer
//
// For loopback tests, setup an audio_capturer interface
void AudioAdminTest::SetUpCapturer(unsigned int index, fuchsia::media::AudioCaptureUsage usage,
                                   int16_t data) {
  FX_CHECK(index < fbl::count_of(audio_capturer_)) << "Capturer index too high";

  int16_t* buffer;
  zx::vmo capture_vmo;

  audio_core_sync_->CreateAudioCapturer(true, audio_capturer_[index].NewRequest());

  audio_capturer_[index].set_error_handler(ErrorHandler());

  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format.channels = kChannelCount;
  format.frames_per_second = kSampleRate;

  capture_sample_size_[index] = sizeof(int16_t);

  capture_size_[index] =
      format.frames_per_second * format.channels * capture_sample_size_[index] * kSampleSeconds;

  // ZX_VM_PERM_WRITE taken here as we pre-fill the buffer to catch any
  // cases where we get back a packet without anything having been done
  // with it.
  zx_status_t status = capture_buffer_[index].CreateAndMap(
      capture_size_[index], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &capture_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Capturer VmoMapper:::CreateAndMap failed - " << status;

  buffer = reinterpret_cast<int16_t*>(capture_buffer_[index].start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++) {
    buffer[i] = data;
  }

  audio_capturer_[index]->SetUsage(usage);
  audio_capturer_[index]->SetPcmStreamType(format);
  audio_capturer_[index]->AddPayloadBuffer(0, std::move(capture_vmo));
  // All audio capturers, by default, are set to 0 dB unity gain (passthru).
}

zx_duration_t AudioAdminTest::GetMinLeadTime(size_t num_renderers) {
  zx_duration_t max_min_lead_time = 0;
  for (auto renderer_num = 0u; renderer_num < num_renderers; ++renderer_num) {
    // Get the minimum duration after submitting a packet to when we can start
    // capturing what we sent on the loopback interface.  We use the longest
    // latency of any of the renderers, but they should all have the same value.
    auto min_leadtime_update = [&max_min_lead_time](zx_duration_t t) {
      max_min_lead_time = std::max(max_min_lead_time, t);
    };

    audio_renderer_[renderer_num].events().OnMinLeadTimeChanged =
        CompletionCallback(min_leadtime_update);

    audio_renderer_[renderer_num]->GetMinLeadTime(CompletionCallback(min_leadtime_update));

    ExpectCallback();
  }
  return max_min_lead_time;
}

// SingleRenderStream
//
// Creates a single output stream and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, SingleRenderStream) {
  // Setup a policy rule that MEDIA being active will not affect a BACKGROUND
  // capture.
  audio_core_sync_->ResetInteractions();

  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }

  fuchsia::media::StreamPacket packet, captured;

  // SetUp playback stream
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime(1);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = playback_size_[0];

  audio_renderer_[0]->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  audio_renderer_[0]->Play(zx::clock::get_monotonic().get(), 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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

  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_[0].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Capture 10 samples of audio.
  audio_capturer_[0]->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_[0], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], kPlaybackData1);
  }

  CleanUpRenderer(0);
}

// RenderMuteCapture
//
// Creates a single output stream and a loopback capture that is muted due to
// the output stream and verifies it gets silence on it.
TEST_F(AudioAdminTest, RenderMuteCapture) {
  // Setup a policy rule that MEDIA being active will mute a BACKGROUND
  // capture.
  audio_core_sync_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::MUTE);
  }

  fuchsia::media::StreamPacket packet, captured;

  // SetUp playback stream
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::SYSTEM_AGENT, kPlaybackData1);
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_[0].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime(1);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = playback_size_[0];

  audio_renderer_[0]->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  audio_renderer_[0]->Play(zx::clock::get_monotonic().get(), 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  audio_capturer_[0]->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_[0], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], 0x0);
  }

  CleanUpRenderer(0);
}

// CaptureMuteRender
//
// Creates a single output stream and a loopback capture and verifies that the
// Render stream is muted in the capturer.
TEST_F(AudioAdminTest, CaptureMuteRender) {
  // Setup a policy rule that MEDIA being active will mute a BACKGROUND
  // capture.
  audio_core_sync_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_capture_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::MUTE);
  }

  fuchsia::media::StreamPacket packet, captured;

  // SetUp playback stream
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData1);
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_[0].start());

  // Start the capturer so that it affects policy, but we don't care about what
  // we receive yet, so don't register for OnPacketProduced.
  audio_capturer_[0]->StartAsyncCapture(10);

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime(1);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = playback_size_[0];

  audio_renderer_[0]->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  audio_renderer_[0]->Play(zx::clock::get_monotonic().get(), 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  bool produced_packet = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([this, &captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
          audio_capturer_[0]->StopAsyncCaptureNoReply();
        }
      });

  // Capture 10 samples of audio.
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_[0], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], 0x0);
  }

  CleanUpRenderer(0);
}

// DualRenderStreamMix
//
// Creates a pair of output streams with different usages that the policy is to
// mix together, and a loopback capture and verifies it gets back what it puts
// in.
TEST_F(AudioAdminTest, DualRenderStreamMix) {
  fuchsia::media::StreamPacket packet[2], captured;

  // Setup expected behavior from policy for this test
  audio_core_sync_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }
  // SetUp playback streams
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  SetUpRenderer(1, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData2);

  // SetUp loopback capture
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_[0].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime(2);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (int i = 0; i < 2; i++) {
    packet[i].payload_offset = 0;
    packet[i].payload_size = playback_size_[i];
    audio_renderer_[i]->SendPacketNoReply(packet[i]);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  audio_renderer_[0]->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  audio_renderer_[1]->Play(playat, 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  audio_capturer_[0]->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_[0], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured.payload_offset + i) % 8000;
    EXPECT_EQ(capture[index], kPlaybackData1 + kPlaybackData2);
  }

  CleanUpRenderer(1);
  CleanUpRenderer(0);
}

// DualRenderStreamDucking
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamDucking) {
  fuchsia::media::StreamPacket packet[2], captured;

  // Setup expected behavior from policy for this test
  audio_core_sync_->ResetInteractions();

  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::INTERRUPTION);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::DUCK);
  }
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::INTERRUPTION);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }
  // SetUp playback streams
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  SetUpRenderer(1, fuchsia::media::AudioRenderUsage::INTERRUPTION, kPlaybackData2);

  // SetUp loopback capture
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_[0].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime(2);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (int i = 0; i < 2; i++) {
    packet[i].payload_offset = 0;
    packet[i].payload_size = playback_size_[i];
    audio_renderer_[i]->SendPacketNoReply(packet[i]);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  audio_renderer_[0]->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  audio_renderer_[1]->Play(playat, 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  audio_capturer_[0]->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_[0], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured.payload_offset + i) % 8000;
    EXPECT_EQ(capture[index], kDuckedPlaybackData1 + kPlaybackData2);
  }

  CleanUpRenderer(1);
  CleanUpRenderer(0);
}

// DualRenderStreamMute
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamMute) {
  fuchsia::media::StreamPacket packet[2], captured;

  // Setup expected behavior from policy for this test
  audio_core_sync_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::MUTE);
  }
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }
  // SetUp playback streams
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  SetUpRenderer(1, fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData2);

  // SetUp loopback capture
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_[0].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime(1);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (int i = 0; i < 2; i++) {
    packet[i].payload_offset = 0;
    packet[i].payload_size = playback_size_[i];
    audio_renderer_[i]->SendPacketNoReply(packet[i]);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  audio_renderer_[0]->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  audio_renderer_[1]->Play(playat, 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  audio_capturer_[0]->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_[0], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], kPlaybackData1);
  }

  CleanUpRenderer(1);
  CleanUpRenderer(0);
}

// DualCaptureStreamNone
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams both remain unaffected.
TEST_F(AudioAdminTest, DualCaptureStreamNone) {
  fuchsia::media::StreamPacket packet[1], captured[2];

  // Setup expected behavior from policy for this test
  audio_core_sync_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::NONE);
  }

  // SetUp playback streams
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);

  // SetUp loopback capture
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);
  SetUpCapturer(1, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture1 = reinterpret_cast<int16_t*>(capture_buffer_[0].start());
  auto* capture2 = reinterpret_cast<int16_t*>(capture_buffer_[1].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet1 = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured[0].payload_size == 0) {
          captured[0] = packet;
          produced_packet1 = true;
        }
      });

  bool produced_packet2 = false;
  audio_capturer_[1].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet2](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured[1].payload_size == 0) {
          captured[1] = packet;
          produced_packet2 = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime(1);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet[0].payload_offset = 0;
  packet[0].payload_size = playback_size_[0];
  audio_renderer_[0]->SendPacketNoReply(packet[0]);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  audio_renderer_[0]->Play(playat, 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  audio_capturer_[0]->StartAsyncCapture(10);
  audio_capturer_[1]->StartAsyncCapture(10);
  RunLoopUntil(
      [&produced_packet1, &produced_packet2]() { return produced_packet1 && produced_packet2; });

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured[0].payload_size / capture_sample_size_[0], 10U);
  EXPECT_EQ(captured[1].payload_size / capture_sample_size_[1], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured[0].payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured[0].payload_offset + i) % 8000;

    EXPECT_EQ(capture1[index], kPlaybackData1);
  }

  for (size_t i = 0; i < (captured[1].payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured[1].payload_offset + i) % 8000;
    EXPECT_EQ(capture2[index], kPlaybackData1);
  }
  CleanUpRenderer(0);
}

// DualCaptureStreamMute
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams of different usages can mute each other.
TEST_F(AudioAdminTest, DualCaptureStreamMute) {
  fuchsia::media::StreamPacket packet[1], captured[2];

  // Setup expected behavior from policy for this test
  audio_core_sync_->ResetInteractions();
  {
    fuchsia::media::Usage active, affected;
    active.set_capture_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
    affected.set_capture_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
    audio_core_sync_->SetInteraction(std::move(active), std::move(affected),
                                     fuchsia::media::Behavior::MUTE);
  }

  // SetUp playback streams
  SetUpRenderer(0, fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);

  // SetUp loopback capture
  SetUpCapturer(0, fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT, kInitialCaptureData);
  SetUpCapturer(1, fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture1 = reinterpret_cast<int16_t*>(capture_buffer_[0].start());
  auto* capture2 = reinterpret_cast<int16_t*>(capture_buffer_[1].start());

  // Add a callback for when we get our captured packet.
  bool produced_packet1 = false;
  audio_capturer_[0].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured[0].payload_size == 0) {
          captured[0] = packet;
          produced_packet1 = true;
        }
      });

  bool produced_packet2 = false;
  audio_capturer_[1].events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet2](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured[1].payload_size == 0) {
          captured[1] = packet;
          produced_packet2 = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime(1);
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet[0].payload_offset = 0;
  packet[0].payload_size = playback_size_[0];
  audio_renderer_[0]->SendPacketNoReply(packet[0]);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  audio_renderer_[0]->Play(playat, 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
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
  audio_capturer_[0]->StartAsyncCapture(10);
  audio_capturer_[1]->StartAsyncCapture(10);
  RunLoopUntil(
      [&produced_packet1, &produced_packet2]() { return produced_packet1 && produced_packet2; });

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured[0].payload_size / capture_sample_size_[0], 10U);
  EXPECT_EQ(captured[1].payload_size / capture_sample_size_[1], 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured[0].payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured[0].payload_offset + i) % 8000;

    EXPECT_EQ(capture1[index], kPlaybackData1);
  }

  for (size_t i = 0; i < (captured[1].payload_size / capture_sample_size_[0]); i++) {
    size_t index = (captured[1].payload_offset + i) % 8000;
    EXPECT_EQ(capture2[index], 0);
  }
  CleanUpRenderer(0);
}

}  // namespace media::audio::test
