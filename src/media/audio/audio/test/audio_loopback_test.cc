// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/lib/test/audio_test_base.h"

namespace media::audio::test {

//
// AudioLoopbackTest
//
// Base Class for testing simple playback and capture with loopback.
class AudioLoopbackTest : public media::audio::test::AudioTestBase {
 public:
  static void SetAudioSync(fuchsia::media::AudioSyncPtr audio_sync) {
    AudioLoopbackTest::audio_sync_ = std::move(audio_sync);
  }

  static void SetVirtualAudioControlSync(
      fuchsia::virtualaudio::ControlSyncPtr virtual_audio_control_sync) {
    AudioLoopbackTest::virtual_audio_control_sync_ = std::move(virtual_audio_control_sync);
  }

  static void SetVirtualAudioOutputSync(
      fuchsia::virtualaudio::OutputSyncPtr virtual_audio_output_sync) {
    AudioLoopbackTest::virtual_audio_output_sync_ = std::move(virtual_audio_output_sync);
  }

 protected:
  static constexpr int32_t kSampleRate = 8000;
  static constexpr int kChannelCount = 1;
  static constexpr int kSampleSeconds = 1;
  static constexpr int16_t kInitialCaptureData = 0x7fff;
  static constexpr unsigned int kMaxNumRenderers = 16;
  static constexpr int16_t kPlaybackData[] = {0x1000, 0xfff,   -0x2345, -0x0123, 0x100,   0xff,
                                              -0x234, -0x04b7, 0x0310,  0x0def,  -0x0101, -0x2020,
                                              0x1357, 0x1324,  0x0135,  0x0132};

  void SetUp() override;
  void TearDown() override;

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

  static fuchsia::media::AudioSyncPtr audio_sync_;
  static fuchsia::virtualaudio::ControlSyncPtr virtual_audio_control_sync_;
  static fuchsia::virtualaudio::OutputSyncPtr virtual_audio_output_sync_;
};

fuchsia::media::AudioSyncPtr AudioLoopbackTest::audio_sync_;
fuchsia::virtualaudio::ControlSyncPtr AudioLoopbackTest::virtual_audio_control_sync_;
fuchsia::virtualaudio::OutputSyncPtr AudioLoopbackTest::virtual_audio_output_sync_;

// The AudioLoopbackEnvironment class allows us to make configuration changes
// before any test case begins, and after all test cases complete.
class AudioLoopbackEnvironment : public testing::Environment {
 public:
  // Do any binary-wide or cross-test-suite setup, before any test suite runs.
  // Note: if --gtest_repeat is used, this is called at start of EVERY repeat.
  //
  // Before any test cases in this program, synchronously connect to the service
  // to ensure that the audio and audio_core components are present and loaded,
  // and that at least one (virtual) audio output device is present.
  //
  // On assert-false during this SetUp method, no test cases run, and they may
  // display as passed. However, the overall binary returns non-zero (fail).
  void SetUp() override {
    testing::Environment::SetUp();

    async::Loop loop(&kAsyncLoopConfigAttachToThread);

    // This is an unchanging input for the entire component; get it once here.
    auto startup_context = sys::ComponentContext::Create();

    // Only connect to services we can use synchronously once per suite.
    fuchsia::media::AudioSyncPtr audio_sync;
    startup_context->svc()->Connect(audio_sync.NewRequest());
    ASSERT_TRUE(audio_sync.is_bound());
    AudioLoopbackTest::SetAudioSync(std::move(audio_sync));

    fuchsia::virtualaudio::ControlSyncPtr virtual_audio_control_sync;
    startup_context->svc()->Connect(virtual_audio_control_sync.NewRequest());
    ASSERT_TRUE(virtual_audio_control_sync.is_bound());
    AudioLoopbackTest::SetVirtualAudioControlSync(std::move(virtual_audio_control_sync));

    fuchsia::virtualaudio::OutputSyncPtr virtual_audio_output_sync;
    startup_context->svc()->Connect(virtual_audio_output_sync.NewRequest());
    ASSERT_TRUE(virtual_audio_output_sync.is_bound());
    AudioLoopbackTest::SetVirtualAudioOutputSync(std::move(virtual_audio_output_sync));

    AudioTestBase::SetStartupContext(std::move(startup_context));
  }

  // Do any last cleanup, after all test suites in this binary have completed.
  // Note: if --gtest_repeat is used, this is called at end of EVERY repeat.
  //
  // void TearDown() override {}

  ///// If needed, this (overriding) function would also need to be public.
  //
  // Unlike TearDown, this is called once after all repetition has concluded.
  //
  //    ~AudioLoopbackEnvironment() override {}
};

// AudioLoopbackTest implementation
//
void AudioLoopbackTest::SetUp() {
  AudioTestBase::SetUp();

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

  audio_dev_enum_.set_error_handler(ErrorHandler());

  startup_context_->svc()->Connect(audio_dev_enum_.NewRequest());
  ASSERT_TRUE(audio_dev_enum_.is_bound());
  // We need at least one active audio output, for loopback capture to work.
  // So use this Control to enable virtualaudio and add a virtual audio
  // output that will exist for the entirety of this binary's test cases. We
  // will remove it and disable virtual_audio immediately afterward.
  zx_status_t status = virtual_audio_control_sync_->Enable();
  ASSERT_EQ(status, ZX_OK) << "Failed to enable virtualaudio";

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

  // Create an output device using default settings, save it while tests run.
  status = virtual_audio_output_sync_->SetUniqueId(dev_uuid);
  ASSERT_EQ(status, ZX_OK) << "Failed to set virtual audio output uuid";

  status = virtual_audio_output_sync_->Add();
  ASSERT_EQ(status, ZX_OK) << "Failed to add virtual audio output";

  // Wait for OnDeviceAdded and OnDefaultDeviceChanged callback.  These will
  // both need to have happened for the new device to be used for the test.
  ExpectCondition([this, &default_dev]() {
    return virtual_audio_output_token_ != 0 && default_dev == virtual_audio_output_token_;
  });

  ASSERT_EQ(virtual_audio_output_token_, default_dev)
      << "Timed out waiting for audio_core to make the virtual audio output "
         "the default.";

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

  audio_sync_->SetSystemGain(0.0f);
  audio_sync_->SetSystemMute(false);
}

void AudioLoopbackTest::TearDown() {
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
  }

  ExpectCondition([&removed]() { return removed; });

  // And ensure that virtualaudio is disabled, by the time we leave.
  if (virtual_audio_control_sync_.is_bound()) {
    zx_status_t status = virtual_audio_control_sync_->Disable();
    ASSERT_EQ(status, ZX_OK) << "Failed to disable virtualaudio";
  }

  EXPECT_TRUE(audio_sync_.is_bound());

  audio_capturer_.Unbind();
  for (auto& renderer : audio_renderer_) {
    renderer.Unbind();
  }

  AudioTestBase::TearDown();
}

// SetUpRenderer
//
// For loopback tests, setup the first audio_renderer interface.
void AudioLoopbackTest::SetUpRenderer(unsigned int index, int16_t data) {
  static_assert(kMaxNumRenderers == fbl::count_of(kPlaybackData),
                "Incorrect amount of kPlaybackData specified");

  FXL_CHECK(index < kMaxNumRenderers) << "Renderer index too high";

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
      format.frames_per_second * format.channels * playback_sample_size_[index] * kSampleSeconds;

  zx_status_t status = payload_buffer_[index].CreateAndMap(
      playback_size_[index], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &payload_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Renderer VmoMapper:::CreateAndMap(" << index << ") failed - "
                           << status;

  buffer = reinterpret_cast<int16_t*>(payload_buffer_[index].start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++)
    buffer[i] = data;

  audio_renderer_[index]->SetPcmStreamType(format);
  audio_renderer_[index]->AddPayloadBuffer(0, std::move(payload_vmo));

  // All audio renderers, by default, are set to 0 dB unity gain (passthru).
}

// CleanUpRenderer
//
// Flush the output and free the vmo that was used by Renderer1.
void AudioLoopbackTest::CleanUpRenderer(unsigned int index) {
  FXL_CHECK(index < kMaxNumRenderers) << "Renderer index too high";

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
  capture_frames_ = format.frames_per_second * kSampleSeconds;
  capture_size_ = capture_frames_ * format.channels * capture_sample_size_;

  // ZX_VM_PERM_WRITE taken here as we pre-fill the buffer to catch cases where
  // we get back a packet without anything having been done with it.
  zx_status_t status = capture_buffer_.CreateAndMap(
      capture_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &capture_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Capturer VmoMapper:::CreateAndMap failed - " << status;

  buffer = reinterpret_cast<int16_t*>(capture_buffer_.start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++)
    buffer[i] = data;

  audio_capturer_->SetPcmStreamType(format);
  audio_capturer_->AddPayloadBuffer(0, std::move(capture_vmo));

  // All audio capturers, by default, are set to 0 dB unity gain (passthru).
}

void AudioLoopbackTest::TestLoopback(unsigned int num_renderers) {
  FXL_CHECK(num_renderers <= kMaxNumRenderers);
  fuchsia::media::StreamPacket packet[kMaxNumRenderers];

  // SetUp loopback capture
  SetUpCapturer(kInitialCaptureData);

  zx_duration_t sleep_duration = 0;
  int16_t expected_val = 0;

  // SetUp playback streams
  for (auto renderer_num = 0u; renderer_num < num_renderers; ++renderer_num) {
    SetUpRenderer(renderer_num, kPlaybackData[renderer_num]);
    expected_val += kPlaybackData[renderer_num];

    // Get the minimum duration after submitting a packet to when we can start
    // capturing what we sent on the loopback interface.  This assumes that the
    // latency will be the same for both playback streams.  This happens to be
    // true for this test as we create the renderers with the same parameters,
    // but is not a safe assumption for the general users of this API to make.
    audio_renderer_[renderer_num]->GetMinLeadTime(CompletionCallback(
        [&sleep_duration](zx_duration_t t) { sleep_duration = std::max(sleep_duration, t); }));
    ExpectCallback();

    packet[renderer_num].payload_offset = 0;
    packet[renderer_num].payload_size = playback_size_[renderer_num];

    audio_renderer_[renderer_num]->SendPacketNoReply(packet[renderer_num]);
  }

  sleep_duration += ZX_MSEC(5);  // Give a little wiggle room.

  int64_t ref_time_received = -1;
  int64_t media_time_received = -1;

  // Start playing right now, so that after we've delayed at least 1 leadtime,
  // we should have mixed audio available for capture.  Our playback is sized
  // to be much much larger than our capture to prevent test flakes.
  auto play_at = zx_clock_get_monotonic();
  for (auto renderer_num = 1u; renderer_num < num_renderers; ++renderer_num) {
    audio_renderer_[renderer_num]->PlayNoReply(play_at, 0);
  }
  // Only get the callback for one renderer -- arbitrarily, renderer 0.
  audio_renderer_[0]->Play(play_at, 0,
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

  // Add a callback for when we get our captured packet.
  fuchsia::media::StreamPacket captured;
  audio_capturer_.events().OnPacketProduced =
      CompletionCallback([this, &captured](fuchsia::media::StreamPacket packet) {
        // We only care about the first set of captured samples
        if (captured.payload_size == 0) {
          captured = packet;
          audio_capturer_->StopAsyncCaptureNoReply();
        }
      });

  // Give the playback some time to get mixed.
  zx_nanosleep(zx_deadline_after(sleep_duration));

  // Capture 10 samples of audio.
  audio_capturer_->StartAsyncCapture(10);
  ExpectCallback();

  // Check that we got 10 samples as we expected.
  EXPECT_EQ(captured.payload_size / capture_sample_size_, 10U);

  // Check that all of the samples contain the expected data.
  auto* capture = reinterpret_cast<int16_t*>(capture_buffer_.start());
  for (size_t i = 0; i < (captured.payload_size / capture_sample_size_); i++) {
    size_t index = (captured.payload_offset + i) % capture_frames_;
    EXPECT_EQ(capture[index], expected_val);
  }

  for (auto renderer_num = 0u; renderer_num < num_renderers; ++renderer_num) {
    CleanUpRenderer(renderer_num);
  }
}

// Test Cases
//

// SingleStream
//
// Creates a single output stream and a loopback capture and verifies it gets
// back what it puts in.
TEST_F(AudioLoopbackTest, SingleStream) { TestLoopback(1); }

// ManyStreams
//
// Verifies loopback capture of 16 output streams.
TEST_F(AudioLoopbackTest, ManyStreams) { TestLoopback(16); }

}  // namespace media::audio::test

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  // gtest takes ownership of registered environments: *do not delete them*
  testing::AddGlobalTestEnvironment(new media::audio::test::AudioLoopbackEnvironment);

  int result = RUN_ALL_TESTS();

  return result;
}
