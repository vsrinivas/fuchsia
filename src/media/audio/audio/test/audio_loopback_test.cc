// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

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
// AudioLoopbackTest
//
// Base Class for testing simple playback and capture with loopback.
class AudioLoopbackTest : public media::audio::test::HermeticAudioTest {
 protected:
  static constexpr int32_t kSampleRate = 48000;  // used for playback and capture
  static constexpr int kChannelCount = 1;

  static constexpr unsigned int kMaxNumRenderers = 16;
  static constexpr int kPlaybackSeconds = 1;
  static constexpr int16_t kPlaybackData[] = {0x1000, 0xfff,   -0x2345, -0x0123, 0x100,   0xff,
                                              -0x234, -0x04b7, 0x0310,  0x0def,  -0x0101, -0x2020,
                                              0x1357, 0x1324,  0x0135,  0x0132};

  static constexpr int16_t kInitialCaptureData = 0x7fff;
  static constexpr zx_duration_t kWaitForRenderersDuration = ZX_MSEC(200);
  static constexpr uint kNumSamplesToCapture = 1000;

  static void SetUpTestSuite();
  static void TearDownTestSuite();

  void SetUp() override;
  void TearDown() override;

  void SetUpVirtualAudioOutput();
  void SetVirtualAudioOutputDeviceGain();

  void SetUpRenderer(unsigned int index, int16_t data);
  void CleanUpRenderer(unsigned int index);

  void SetUpCapturer(int16_t data);

  void TestLoopback(unsigned int num_renderers);

  fuchsia::media::AudioRendererPtr audio_renderer_[kMaxNumRenderers];
  fzl::VmoMapper payload_buffer_[kMaxNumRenderers];
  size_t playback_size_[kMaxNumRenderers];
  size_t playback_sample_size_[kMaxNumRenderers];

  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fzl::VmoMapper capture_buffer_;
  size_t capture_sample_size_;
  size_t capture_frames_;
  size_t capture_size_;

  fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum_;
  uint64_t virtual_audio_output_token_;

  fuchsia::media::AudioSyncPtr audio_sync_;
  fuchsia::virtualaudio::OutputSyncPtr virtual_audio_output_sync_;
};

// static
void AudioLoopbackTest::SetUpTestSuite() {
  HermeticAudioTest::SetUpTestSuite();

  // Ensure that virtualaudio is enabled, before testing commences.
  fuchsia::virtualaudio::ControlSyncPtr control_sync;
  environment()->ConnectToService(control_sync.NewRequest());
  ASSERT_EQ(ZX_OK, control_sync->Enable());
}

// static
void AudioLoopbackTest::TearDownTestSuite() {
  // Ensure that virtualaudio is disabled, by the time we leave.
  fuchsia::virtualaudio::ControlSyncPtr control_sync;
  environment()->ConnectToService(control_sync.NewRequest());
  ASSERT_EQ(ZX_OK, control_sync->Disable());

  HermeticAudioTest::TearDownTestSuite();
}

// AudioLoopbackTest implementation
//
void AudioLoopbackTest::SetUp() {
  HermeticAudioTest::SetUp();

  environment()->ConnectToService(audio_dev_enum_.NewRequest());
  ASSERT_TRUE(audio_dev_enum_.is_bound());
  audio_dev_enum_.set_error_handler(ErrorHandler());

  SetUpVirtualAudioOutput();

  audio_dev_enum_.events().OnDeviceAdded =
      CompletionCallback([](fuchsia::media::AudioDeviceInfo unused) {
        ASSERT_TRUE(false) << "Audio device added while test was running";
      });

  audio_dev_enum_.events().OnDeviceRemoved = CompletionCallback([this](uint64_t token) {
    ASSERT_FALSE(token == virtual_audio_output_token_)
        << "Audio device removed while test was running";
  });

  audio_dev_enum_.events().OnDefaultDeviceChanged =
      CompletionCallback([](uint64_t old_default_token, uint64_t new_default_token) {
        ASSERT_TRUE(false) << "Default route changed while test was running.";
      });

  environment()->ConnectToService(audio_sync_.NewRequest());

  SetVirtualAudioOutputDeviceGain();
}

void AudioLoopbackTest::TearDown() {
  audio_capturer_.Unbind();
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
  audio_dev_enum_.events().OnDeviceGainChanged = nullptr;

  // Remove our virtual audio output device
  if (virtual_audio_output_sync_.is_bound()) {
    zx_status_t status = virtual_audio_output_sync_->Remove();
    ASSERT_EQ(status, ZX_OK) << "Failed to remove virtual audio output";

    virtual_audio_output_sync_.Unbind();
  }

  RunLoopUntil([&removed]() { return removed; });

  EXPECT_TRUE(audio_dev_enum_.is_bound());
  EXPECT_TRUE(audio_sync_.is_bound());

  HermeticAudioTest::TearDown();
}

// SetUpVirtualAudioOutput
//
// For loopback tests, setup the required audio output, using virtualaudio.
void AudioLoopbackTest::SetUpVirtualAudioOutput() {
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

// Once the virtual audio output device is in place, set its device gain to unity (0 dB). We must do
// this even though the device is virtual, because currently we implement device gain in software.
void AudioLoopbackTest::SetVirtualAudioOutputDeviceGain() {
  audio_dev_enum_.events().OnDeviceGainChanged = nullptr;
  audio_dev_enum_->SetDeviceGain(virtual_audio_output_token_,
                                 fuchsia::media::AudioGainInfo{
                                     .gain_db = 0.0,
                                     .flags = 0,
                                 },
                                 fuchsia::media::SetAudioGainFlag_GainValid);

  fuchsia::media::AudioGainInfo gain_info{
      .gain_db = -160.0f,
      .flags = -1u,
  };
  while (gain_info.gain_db != 0.0f &&
         (gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute) != 0u) {
    uint64_t gain_adjusted_token = 0;
    audio_dev_enum_->GetDeviceGain(
        virtual_audio_output_token_,
        [&gain_adjusted_token, &gain_info](auto token, auto new_gain_info) {
          gain_info = new_gain_info;
          gain_adjusted_token = token;
        });

    RunLoopUntil([this, &gain_adjusted_token]() {
      return gain_adjusted_token == virtual_audio_output_token_;
    });
  }

  EXPECT_EQ(gain_info.gain_db, 0.0f);
  EXPECT_EQ(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute, 0u);
}

// SetUpRenderer
//
// For loopback tests, setup the first audio_renderer interface.
void AudioLoopbackTest::SetUpRenderer(unsigned int index, int16_t data) {
  static_assert(kMaxNumRenderers == fbl::count_of(kPlaybackData),
                "Incorrect amount of kPlaybackData specified");

  FX_CHECK(index < kMaxNumRenderers) << "Renderer index too high";

  int16_t* buffer;
  zx::vmo payload_vmo;

  audio_sync_->CreateAudioRenderer(audio_renderer_[index].NewRequest());
  ASSERT_TRUE(audio_renderer_[index].is_bound());

  audio_renderer_[index].set_error_handler(ErrorHandler());

  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format.channels = kChannelCount;
  format.frames_per_second = kSampleRate;

  playback_sample_size_[index] = sizeof(int16_t);

  playback_size_[index] =
      format.frames_per_second * format.channels * playback_sample_size_[index] * kPlaybackSeconds;

  zx_status_t status = payload_buffer_[index].CreateAndMap(
      playback_size_[index], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &payload_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Renderer VmoMapper::CreateAndMap(" << index << ") failed - "
                           << status;

  buffer = reinterpret_cast<int16_t*>(payload_buffer_[index].start());
  for (int32_t i = 0; i < kSampleRate * kPlaybackSeconds; ++i) {
    buffer[i] = data;
  }

  audio_renderer_[index]->SetPcmStreamType(format);
  audio_renderer_[index]->AddPayloadBuffer(0, std::move(payload_vmo));

  // All audio renderers, by default, are set to 0 dB unity gain (passthru).
}

// CleanUpRenderer
//
// Flush the output and free the vmo that was used by this Renderer.
void AudioLoopbackTest::CleanUpRenderer(unsigned int index) {
  FX_CHECK(index < kMaxNumRenderers) << "Renderer index too high";

  // Flush the audio
  audio_renderer_[index]->DiscardAllPackets(CompletionCallback());
  ExpectCallback();

  payload_buffer_[index].Unmap();
}

// SetUpCapturer
//
// For loopback tests, setup an audio_capturer interface
void AudioLoopbackTest::SetUpCapturer(int16_t data) {
  int16_t* buffer;
  zx::vmo capture_vmo;

  audio_sync_->CreateAudioCapturer(audio_capturer_.NewRequest(), true);
  ASSERT_TRUE(audio_capturer_.is_bound());

  audio_capturer_.set_error_handler(ErrorHandler());

  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format.channels = kChannelCount;
  format.frames_per_second = kSampleRate;

  capture_sample_size_ = sizeof(int16_t);
  capture_frames_ = format.frames_per_second * kPlaybackSeconds;
  capture_size_ = capture_frames_ * format.channels * capture_sample_size_;

  // ZX_VM_PERM_WRITE taken here as we pre-fill the buffer to catch cases where we get back a packet
  // without anything having been done with it.
  zx_status_t status = capture_buffer_.CreateAndMap(
      capture_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &capture_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Capturer VmoMapper::CreateAndMap failed - " << status;

  buffer = reinterpret_cast<int16_t*>(capture_buffer_.start());
  for (int32_t i = 0; i < kSampleRate * kPlaybackSeconds; ++i) {
    buffer[i] = data;
  }

  audio_capturer_->SetPcmStreamType(format);
  audio_capturer_->AddPayloadBuffer(0, std::move(capture_vmo));

  // All audio capturers, by default, are set to 0 dB unity gain (passthru).
}

void AudioLoopbackTest::TestLoopback(unsigned int num_renderers) {
  FX_CHECK(num_renderers <= kMaxNumRenderers);
  fuchsia::media::StreamPacket packet[kMaxNumRenderers];

  // SetUp loopback capture
  SetUpCapturer(kInitialCaptureData);

  // Add a callback for when we get our captured packet.
  // We do this before our sequence of "start all the renderers, then start capturing".
  fuchsia::media::StreamPacket capture_packet;
  bool received_first_packet = false;

  audio_capturer_.events().OnPacketProduced = CompletionCallback(
      [this, &capture_packet, &received_first_packet](fuchsia::media::StreamPacket packet) {
        // We only care about the first packet of captured samples
        if (received_first_packet) {
          FX_LOGS(WARNING) << "Ignoring subsequent packet: pts " << packet.pts << ", start "
                           << packet.payload_offset << ", size " << packet.payload_size
                           << ", flags 0x" << std::hex << packet.flags;
        } else if (packet.payload_size == 0) {
          FX_LOGS(WARNING) << "Ignoring empty packet: pts " << packet.pts << ", start "
                           << packet.payload_offset << ", size " << packet.payload_size
                           << ", flags 0x" << std::hex << packet.flags;
        } else {
          received_first_packet = true;
          capture_packet = std::move(packet);
          audio_capturer_->StopAsyncCaptureNoReply();
        }
      });

  zx_duration_t sleep_duration = 0;
  int16_t expected_val = 0;

  // SetUp playback streams, including determining the needed lead time and submitting packets.
  for (auto renderer_num = 0u; renderer_num < num_renderers; ++renderer_num) {
    SetUpRenderer(renderer_num, kPlaybackData[renderer_num]);
    expected_val += kPlaybackData[renderer_num];

    // Get our expected duration, from a packet submittal to when we can start capturing what we
    // sent on the loopback interface. After our Use the largest 'min_lead_time' across all of our
    // renderers.
    audio_renderer_[renderer_num]->GetMinLeadTime(
        CompletionCallback([&sleep_duration](zx_duration_t min_lead_time) {
          sleep_duration = std::max(sleep_duration, min_lead_time);
        }));
    ExpectCallback();

    packet[renderer_num].payload_offset = 0;
    packet[renderer_num].payload_size = playback_size_[renderer_num];

    audio_renderer_[renderer_num]->SendPacketNoReply(packet[renderer_num]);
  }

  sleep_duration += kWaitForRenderersDuration;

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing now, so that after at least 1 leadtime, mixed audio is available to capture.
  // Playback is sized much much larger than our capture to prevent test flakes.
  auto play_at = zx::clock::get_monotonic().get() + sleep_duration + ZX_MSEC(1);

  // Only get the callback for one renderer -- arbitrarily, renderer 0.
  audio_renderer_[0]->Play(play_at, 0,
                           CompletionCallback([&ref_time_received, &media_time_received](
                                                  int64_t ref_time, int64_t media_time) {
                             ref_time_received = ref_time;
                             media_time_received = media_time;
                           }));
  ExpectCallback();
  EXPECT_EQ(media_time_received, 0);
  EXPECT_GT(ref_time_received, 0);

  // Start the other renderers at exactly the same [ref_time, media_time] correspondence.
  for (auto renderer_num = 1u; renderer_num < num_renderers; ++renderer_num) {
    audio_renderer_[renderer_num]->PlayNoReply(ref_time_received, media_time_received);
  }

  // We expect that media_time 0 played back at some point after the 'zero' time on the system.

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture kNumSamplesToCapture samples of audio.
  audio_capturer_->StartAsyncCapture(kNumSamplesToCapture);

  ExpectCallback();
  EXPECT_TRUE(received_first_packet);

  // Check that we got kNumSamplesToCapture samples as we expected.
  EXPECT_EQ(capture_packet.payload_size / capture_sample_size_, kNumSamplesToCapture);

  auto previous_val = expected_val;
  // Check that all of the samples contain the expected data.
  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_.start());
  for (size_t i = 0; i < kNumSamplesToCapture; ++i) {
    if (capture[capture_packet.payload_offset + i] != previous_val) {
      previous_val = capture[capture_packet.payload_offset + i];
      std::cout << "At [" << capture_packet.payload_offset + i << "], wanted " << expected_val
                << ", got " << capture[capture_packet.payload_offset + i];

      EXPECT_EQ(capture[capture_packet.payload_offset + i], expected_val)
          << capture_packet.payload_offset + i;
    }
  }

  for (auto renderer_num = 0u; renderer_num < num_renderers; ++renderer_num) {
    CleanUpRenderer(renderer_num);
  }
}

// Test Cases
//

// SingleStream
//
// Create one output stream and one loopback capture, and verify we receive what we sent out.
TEST_F(AudioLoopbackTest, SingleStream) { TestLoopback(1); }

// ManyStreams
//
// Verify loopback capture of the output mix of 16 renderer streams.
// TODO(fxb/42050): Re-enable this test after the FIDL v1 wire-format transition is complete.
// This test case sometimes times out on CQ/CI bots because of the FIDL v1 transition.
TEST_F(AudioLoopbackTest, DISABLED_ManyStreams) { TestLoopback(16); }

}  // namespace media::audio::test
