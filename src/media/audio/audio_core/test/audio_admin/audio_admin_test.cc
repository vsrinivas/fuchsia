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
  static constexpr int16_t kDuckedPlaybackData1 = 0x4e;  // reduced by 35dB
  static constexpr int16_t kPlaybackData2 = 0x2222;

  static void SetUpTestSuite();
  static void TearDownTestSuite();

  void SetUp() override;
  void TearDown() override;

  void SetUpVirtualAudioOutput();

  template <typename T>
  struct StreamHolder {
    fidl::InterfacePtr<T> stream_ptr;
    fzl::VmoMapper payload_buffer;
    size_t buffer_size;
    size_t sample_size;
  };

  StreamHolder<fuchsia::media::AudioRenderer> SetUpRenderer(fuchsia::media::AudioRenderUsage usage,
                                                            int16_t data);
  StreamHolder<fuchsia::media::AudioCapturer> SetUpCapturer(fuchsia::media::AudioCaptureUsage usage,
                                                            int16_t data);

  zx_duration_t GetMinLeadTime(
      std::initializer_list<
          std::reference_wrapper<const StreamHolder<fuchsia::media::AudioRenderer>>>
          renderers);

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
}

void AudioAdminTest::TearDown() {
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
AudioAdminTest::StreamHolder<fuchsia::media::AudioRenderer> AudioAdminTest::SetUpRenderer(
    fuchsia::media::AudioRenderUsage usage, int16_t data) {
  StreamHolder<fuchsia::media::AudioRenderer> holder;
  int16_t* buffer;
  zx::vmo payload_vmo;

  audio_core_sync_->CreateAudioRenderer(holder.stream_ptr.NewRequest());

  holder.stream_ptr.set_error_handler(ErrorHandler());

  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format.channels = kChannelCount;
  format.frames_per_second = kSampleRate;

  holder.sample_size = sizeof(int16_t);

  holder.buffer_size =
      format.frames_per_second * format.channels * holder.sample_size * kSampleSeconds;

  zx_status_t status = holder.payload_buffer.CreateAndMap(
      holder.buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &payload_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  FX_CHECK(status == ZX_OK) << "Renderer VmoMapper:::CreateAndMap failed - " << status;

  buffer = reinterpret_cast<int16_t*>(holder.payload_buffer.start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++) {
    buffer[i] = data;
  }

  holder.stream_ptr->SetUsage(usage);
  holder.stream_ptr->SetPcmStreamType(format);
  holder.stream_ptr->AddPayloadBuffer(0, std::move(payload_vmo));

  // TODO(41973): Move into device setup.
  audio_dev_enum_->SetDeviceGain(virtual_audio_output_token_,
                                 fuchsia::media::AudioGainInfo{
                                     .gain_db = 0.0,
                                     .flags = 0,
                                 },
                                 fuchsia::media::SetAudioGainFlag_GainValid);

  uint64_t gain_adjusted_token = 0;
  fuchsia::media::AudioGainInfo gain_info;
  audio_dev_enum_->GetDeviceGain(virtual_audio_output_token_, [&gain_adjusted_token, &gain_info](
                                                                  auto token, auto new_gain_info) {
    gain_info = new_gain_info;
    gain_adjusted_token = token;
  });
  RunLoopUntil([this, &gain_adjusted_token]() {
    return gain_adjusted_token == virtual_audio_output_token_;
  });

  // All audio renderers, by default, are set to 0 dB unity gain (passthru).
  return holder;
}

// SetUpCapturer
//
// For loopback tests, setup an audio_capturer interface
AudioAdminTest::StreamHolder<fuchsia::media::AudioCapturer> AudioAdminTest::SetUpCapturer(
    fuchsia::media::AudioCaptureUsage usage, int16_t data) {
  StreamHolder<fuchsia::media::AudioCapturer> holder;
  int16_t* buffer;
  zx::vmo capture_vmo;

  fuchsia::media::AudioCapturerConfiguration configuration;
  configuration.set_loopback(fuchsia::media::LoopbackAudioCapturerConfiguration());

  auto format =
      fuchsia::media::AudioStreamType{.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16,
                                      .channels = kChannelCount,
                                      .frames_per_second = kSampleRate};

  audio_core_sync_->CreateAudioCapturerWithConfiguration(format, usage, std::move(configuration),
                                                         holder.stream_ptr.NewRequest());

  holder.stream_ptr.set_error_handler(ErrorHandler());
  holder.sample_size = sizeof(int16_t);
  holder.buffer_size =
      format.frames_per_second * format.channels * holder.sample_size * kSampleSeconds;

  // ZX_VM_PERM_WRITE taken here as we pre-fill the buffer to catch any
  // cases where we get back a packet without anything having been done
  // with it.
  zx_status_t status = holder.payload_buffer.CreateAndMap(
      holder.buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &capture_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  FX_CHECK(status == ZX_OK) << "Capturer VmoMapper:::CreateAndMap failed - " << status;

  buffer = reinterpret_cast<int16_t*>(holder.payload_buffer.start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++) {
    buffer[i] = data;
  }

  // All audio capturers, by default, are set to 0 dB unity gain (passthru).
  holder.stream_ptr->AddPayloadBuffer(0, std::move(capture_vmo));
  return holder;
}

zx_duration_t AudioAdminTest::GetMinLeadTime(
    std::initializer_list<std::reference_wrapper<const StreamHolder<fuchsia::media::AudioRenderer>>>
        renderers) {
  zx_duration_t max_min_lead_time = 0;
  for (const auto& renderer : renderers) {
    // Get the minimum duration after submitting a packet to when we can start
    // capturing what we sent on the loopback interface.  We use the longest
    // latency of any of the renderers, but they should all have the same value.
    auto min_leadtime_update = [&max_min_lead_time](zx_duration_t t) {
      max_min_lead_time = std::max(max_min_lead_time, t);
    };

    renderer.get().stream_ptr.events().OnMinLeadTimeChanged =
        CompletionCallback(min_leadtime_update);
    renderer.get().stream_ptr->GetMinLeadTime(CompletionCallback(min_leadtime_update));
    ExpectCallback();
    renderer.get().stream_ptr.events().OnMinLeadTimeChanged = nullptr;
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
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = renderer.buffer_size;

  renderer.stream_ptr->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  renderer.stream_ptr->Play(zx::clock::get_monotonic().get(), 0,
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

  auto* capture = reinterpret_cast<int16_t*>(capturer.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  capturer.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Capture 10 samples of audio.
  capturer.stream_ptr->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capturer.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capturer.sample_size); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], kPlaybackData1);
  }
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
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT, kPlaybackData1);
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capturer.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  capturer.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = renderer.buffer_size;

  renderer.stream_ptr->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  renderer.stream_ptr->Play(zx::clock::get_monotonic().get(), 0,
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
  capturer.stream_ptr->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capturer.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capturer.sample_size); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], 0x0);
  }
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
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData1);
  auto capturer =
      SetUpCapturer(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capturer.payload_buffer.start());

  // Start the capturer so that it affects policy, but we don't care about what
  // we receive yet, so don't register for OnPacketProduced.
  capturer.stream_ptr->StartAsyncCapture(10);

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface
  zx_duration_t sleep_duration = GetMinLeadTime({renderer});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  packet.payload_offset = 0;
  packet.payload_size = renderer.buffer_size;

  renderer.stream_ptr->SendPacketNoReply(packet);

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  renderer.stream_ptr->Play(zx::clock::get_monotonic().get(), 0,
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
  capturer.stream_ptr.events().OnPacketProduced = CompletionCallback(
      [&capturer, &captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
          capturer.stream_ptr->StopAsyncCaptureNoReply();
        }
      });

  // Capture 10 samples of audio.
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capturer.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capturer.sample_size); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], 0x0);
  }
}

// DualRenderStreamMix
//
// Creates a pair of output streams with different usages that the policy is to
// mix together, and a loopback capture and verifies it gets back what it puts
// in.
TEST_F(AudioAdminTest, DualRenderStreamMix) {
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
  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData2);

  // SetUp loopback capture
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capturer.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  fuchsia::media::StreamPacket captured;
  capturer.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer1, renderer2});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (auto renderer : {&renderer1, &renderer2}) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = renderer->buffer_size;
    renderer->stream_ptr->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer1.stream_ptr->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  renderer2.stream_ptr->Play(playat, 0,
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
  capturer.stream_ptr->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capturer.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capturer.sample_size); i++) {
    size_t index = (captured.payload_offset + i) % 8000;
    EXPECT_EQ(capture[index], kPlaybackData1 + kPlaybackData2);
  }
}

// DualRenderStreamDucking
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamDucking) {
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
  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::INTERRUPTION, kPlaybackData2);

  // SetUp loopback capture
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capturer.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  fuchsia::media::StreamPacket captured;
  capturer.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer1, renderer2});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (auto renderer : {&renderer1, &renderer2}) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = renderer->buffer_size;
    renderer->stream_ptr->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer1.stream_ptr->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  renderer2.stream_ptr->Play(playat, 0,
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
  capturer.stream_ptr->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capturer.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capturer.sample_size); i++) {
    size_t index = (captured.payload_offset + i) % 8000;
    EXPECT_EQ(capture[index], kDuckedPlaybackData1 + kPlaybackData2);
  }
}

// DualRenderStreamMute
//
// Creates a pair of output streams and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioAdminTest, DualRenderStreamMute) {
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
  auto renderer1 = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);
  auto renderer2 = SetUpRenderer(fuchsia::media::AudioRenderUsage::BACKGROUND, kPlaybackData2);

  // SetUp loopback capture
  auto capturer = SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture = reinterpret_cast<int16_t*>(capturer.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  bool produced_packet = false;
  fuchsia::media::StreamPacket captured;
  capturer.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured, &produced_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          produced_packet = true;
        }
      });

  // Get the minimum duration after submitting a packet to when we can start
  // capturing what we sent on the loopback interface.
  zx_duration_t sleep_duration = GetMinLeadTime({renderer1});
  ASSERT_NE(sleep_duration, 0) << "Failed to get MinLeadTime";

  for (auto renderer : {&renderer1, &renderer2}) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = 0;
    packet.payload_size = renderer->buffer_size;
    renderer->stream_ptr->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer1.stream_ptr->PlayNoReply(playat, 0);
  // Only get the callback for the second renderer.
  renderer2.stream_ptr->Play(playat, 0,
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
  capturer.stream_ptr->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capturer.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured.payload_size / capturer.sample_size); i++) {
    size_t index = (captured.payload_offset + i) % 8000;

    EXPECT_EQ(capture[index], kPlaybackData1);
  }
}

// DualCaptureStreamNone
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams both remain unaffected.
TEST_F(AudioAdminTest, DualCaptureStreamNone) {
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
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);

  // SetUp loopback capture
  auto capturer1 =
      SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);
  auto capturer2 =
      SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture1 = reinterpret_cast<int16_t*>(capturer1.payload_buffer.start());
  auto* capture2 = reinterpret_cast<int16_t*>(capturer2.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  fuchsia::media::StreamPacket captured1;
  bool produced_packet1 = false;
  capturer1.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured1, &produced_packet1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured1.payload_size == 0) {
          captured1 = packet;
          produced_packet1 = true;
        }
      });

  fuchsia::media::StreamPacket captured2;
  bool produced_packet2 = false;
  capturer2.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured2, &produced_packet2](fuchsia::media::StreamPacket packet) {
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
    packet.payload_size = renderer.buffer_size;
    renderer.stream_ptr->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer.stream_ptr->Play(playat, 0,
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
  capturer1.stream_ptr->StartAsyncCapture(10);
  capturer2.stream_ptr->StartAsyncCapture(10);
  RunLoopUntil(
      [&produced_packet1, &produced_packet2]() { return produced_packet1 && produced_packet2; });

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured1.payload_size / capturer1.sample_size, 10U);
  EXPECT_EQ(captured2.payload_size / capturer2.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured1.payload_size / capturer1.sample_size); i++) {
    size_t index = (captured1.payload_offset + i) % 8000;

    EXPECT_EQ(capture1[index], kPlaybackData1);
  }

  for (size_t i = 0; i < (captured2.payload_size / capturer2.sample_size); i++) {
    size_t index = (captured2.payload_offset + i) % 8000;
    EXPECT_EQ(capture2[index], kPlaybackData1);
  }
}

// DualCaptureStreamMute
//
// Creates a pair of loopback capture streams and a render stream and verifies
// capture streams of different usages can mute each other.
TEST_F(AudioAdminTest, DISABLED_DualCaptureStreamMute) {
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
  auto renderer = SetUpRenderer(fuchsia::media::AudioRenderUsage::MEDIA, kPlaybackData1);

  // SetUp loopback capture
  auto capturer1 =
      SetUpCapturer(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT, kInitialCaptureData);
  auto capturer2 =
      SetUpCapturer(fuchsia::media::AudioCaptureUsage::BACKGROUND, kInitialCaptureData);

  auto* capture1 = reinterpret_cast<int16_t*>(capturer1.payload_buffer.start());
  auto* capture2 = reinterpret_cast<int16_t*>(capturer2.payload_buffer.start());

  // Add a callback for when we get our captured packet.
  fuchsia::media::StreamPacket captured1;
  bool produced_packet1 = false;
  capturer1.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured1, &produced_packet1](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured1.payload_size == 0) {
          captured1 = packet;
          produced_packet1 = true;
        }
      });

  fuchsia::media::StreamPacket captured2;
  bool produced_packet2 = false;
  capturer2.stream_ptr.events().OnPacketProduced =
      CompletionCallback([&captured2, &produced_packet2](fuchsia::media::StreamPacket packet) {
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
    packet.payload_size = renderer.buffer_size;
    renderer.stream_ptr->SendPacketNoReply(packet);
  }

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto playat = zx::clock::get_monotonic().get();
  renderer.stream_ptr->Play(playat, 0,
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
  capturer1.stream_ptr->StartAsyncCapture(10);
  capturer2.stream_ptr->StartAsyncCapture(10);
  RunLoopUntil(
      [&produced_packet1, &produced_packet2]() { return produced_packet1 && produced_packet2; });

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured1.payload_size / capturer1.sample_size, 10U);
  EXPECT_EQ(captured2.payload_size / capturer2.sample_size, 10U);

  // Check that all of the samples contain the expected data.
  for (size_t i = 0; i < (captured1.payload_size / capturer1.sample_size); i++) {
    size_t index = (captured1.payload_offset + i) % 8000;

    EXPECT_EQ(capture1[index], kPlaybackData1);
  }

  for (size_t i = 0; i < (captured2.payload_size / capturer2.sample_size); i++) {
    size_t index = (captured2.payload_offset + i) % 8000;
    EXPECT_EQ(capture2[index], 0);
  }
}

}  // namespace media::audio::test
