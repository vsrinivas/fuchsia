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
 protected:
  static constexpr int32_t kSampleRate = 8000;
  static constexpr int kChannelCount = 1;
  static constexpr int kSampleSeconds = 1;
  static constexpr int16_t kInitialCaptureData = 0x7fff;
  static constexpr unsigned int kMaxNumRenderers = 16;
  static constexpr int16_t kPlaybackData[] = {
      0x1000, 0xfff,  -0x2345, -0x0123, 0x100,  0xff,   -0x234, -0x04b7,
      0x0310, 0x0def, -0x0101, -0x2020, 0x1357, 0x1324, 0x0135, 0x0132};

  void SetUp() override;
  void TearDown() override;

  void SetUpRenderer(unsigned int index, int16_t data);
  void CleanUpRenderer(unsigned int index);

  void SetUpCapturer(int16_t data);
  void CleanUpCapturer();

  void TestLoopback(unsigned int num_renderers);

  fuchsia::media::AudioPtr audio_;

  fuchsia::media::AudioRendererPtr audio_renderer_[kMaxNumRenderers];
  fzl::VmoMapper payload_buffer_[kMaxNumRenderers];
  size_t playback_size_[kMaxNumRenderers];
  size_t playback_sample_size_[kMaxNumRenderers];

  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fzl::VmoMapper capture_buffer_;
  size_t capture_sample_size_;
  size_t capture_frames_;
  size_t capture_size_;
};

// std::unique_ptr<sys::ComponentContext> AudioLoopbackTest::startup_context_;

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

    // We need at least one active audio output, for loopback capture to work.
    // So use this Control to enable virtualaudio and add a virtual audio output
    // that will exist for the entirety of this binary's test cases. We will
    // remove it and disable virtual_audio immediately afterward.
    startup_context->svc()->Connect(virtual_audio_control_sync_.NewRequest());
    zx_status_t status = virtual_audio_control_sync_->Enable();
    ASSERT_EQ(status, ZX_OK) << "Failed to enable virtualaudio";

    // Create an output device using default settings, save it while tests run.
    startup_context->svc()->Connect(virtual_audio_output_sync_.NewRequest());
    status = virtual_audio_output_sync_->Add();
    ASSERT_EQ(status, ZX_OK) << "Failed to add virtual audio output";

    // Ensure that the output is active before we proceed to running tests.
    uint32_t num_inputs, num_outputs = 0, num_tries = 0;
    do {
      status =
          virtual_audio_control_sync_->GetNumDevices(&num_inputs, &num_outputs);
      ASSERT_EQ(status, ZX_OK) << "GetNumDevices failed";

      ++num_tries;
    } while (num_outputs == 0 && num_tries < 100);

    ASSERT_GT(num_outputs, 0u)
        << "Timed out trying to add virtual audio output";

    // Synchronously calling a FIDL method with a callback guarantees that the
    // service is loaded and running before the sync method itself returns.
    //
    // This is not the case for sync calls _without_ callback, nor async calls,
    // because of pipelining inherent in FIDL's design.
    startup_context->svc()->Connect(audio_dev_enum_sync_.NewRequest());

    // Ensure that the audio_core binary is resident.
    uint64_t default_output;
    bool connected_to_svc = (audio_dev_enum_sync_->GetDefaultOutputDevice(
                                 &default_output) == ZX_OK);

    ASSERT_TRUE(connected_to_svc) << "Failed in GetDefaultOutputDevice";

    AudioTestBase::SetStartupContext(std::move(startup_context));
  }

  // Do any last cleanup, after all test suites in this binary have completed.
  // Note: if --gtest_repeat is used, this is called at end of EVERY repeat.
  //
  // Ensure that our virtual device is gone when our test bin finishes a run.
  void TearDown() override {
    // Remove our virtual audio output device
    zx_status_t status = virtual_audio_output_sync_->Remove();
    ASSERT_EQ(status, ZX_OK) << "Failed to add virtual audio output";

    // And ensure that virtualaudio is disabled, by the time we leave.
    status = virtual_audio_control_sync_->Disable();
    ASSERT_EQ(status, ZX_OK) << "Failed to disable virtualaudio";

    // Wait for GetNumDevices(output) to equal zero before proceeding.
    uint32_t num_inputs = 1, num_outputs = 1, num_tries = 0;
    do {
      status =
          virtual_audio_control_sync_->GetNumDevices(&num_inputs, &num_outputs);
      ASSERT_EQ(status, ZX_OK) << "GetNumDevices failed";

      ++num_tries;
    } while ((num_outputs != 0 || num_inputs != 0) && num_tries < 100);

    ASSERT_EQ(num_outputs, 0u) << "Timed out while disabling virtualaudio";
    ASSERT_EQ(num_inputs, 0u) << "Timed out while disabling virtualaudio";

    virtual_audio_output_sync_.Unbind();
    virtual_audio_control_sync_.Unbind();
    audio_dev_enum_sync_.Unbind();

    testing::Environment::TearDown();
  }

  ///// If needed, this (overriding) function would also need to be public.
  //
  // Unlike TearDown, this is called once after all repetition has concluded.
  //
  //    ~AudioLoopbackEnvironment() override {}

  fuchsia::virtualaudio::ControlSyncPtr virtual_audio_control_sync_;
  fuchsia::virtualaudio::OutputSyncPtr virtual_audio_output_sync_;
  fuchsia::media::AudioDeviceEnumeratorSyncPtr audio_dev_enum_sync_;
};

// AudioLoopbackTest implementation
//
void AudioLoopbackTest::SetUp() {
  AudioTestBase::SetUp();

  startup_context_->svc()->Connect(audio_.NewRequest());
  ASSERT_TRUE(audio_.is_bound());

  audio_.set_error_handler(ErrorHandler());
  audio_->SetSystemGain(0.0f);
  audio_->SetSystemMute(false);
}

void AudioLoopbackTest::TearDown() {
  EXPECT_TRUE(audio_.is_bound());

  audio_capturer_.Unbind();
  for (auto& renderer : audio_renderer_) {
    renderer.Unbind();
  }
  audio_.Unbind();

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

  audio_->CreateAudioRenderer(audio_renderer_[index].NewRequest());
  ASSERT_TRUE(audio_renderer_[index].is_bound());

  audio_renderer_[index].set_error_handler(ErrorHandler());

  fuchsia::media::AudioStreamType format;
  format.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  format.channels = kChannelCount;
  format.frames_per_second = kSampleRate;

  playback_sample_size_[index] = sizeof(int16_t);

  playback_size_[index] = format.frames_per_second * format.channels *
                          playback_sample_size_[index] * kSampleSeconds;

  zx_status_t status = payload_buffer_[index].CreateAndMap(
      playback_size_[index], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
      &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  ASSERT_EQ(status, ZX_OK) << "Renderer VmoMapper:::CreateAndMap(" << index
                           << ") failed - " << status;

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

  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), true);
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
  ASSERT_EQ(status, ZX_OK) << "Capturer VmoMapper:::CreateAndMap failed - "
                           << status;

  buffer = reinterpret_cast<int16_t*>(capture_buffer_.start());
  for (int32_t i = 0; i < kSampleRate * kSampleSeconds; i++)
    buffer[i] = data;

  audio_capturer_->SetPcmStreamType(format);
  audio_capturer_->AddPayloadBuffer(0, std::move(capture_vmo));

  // All audio capturers, by default, are set to 0 dB unity gain (passthru).
}

// CleanUpCapturer
//
void AudioLoopbackTest::CleanUpCapturer() {
  if (audio_capturer_.is_bound()) {
    audio_capturer_->DiscardAllPacketsNoReply();
  }
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
    audio_renderer_[renderer_num]->GetMinLeadTime(
        CompletionCallback([&sleep_duration](zx_duration_t t) {
          sleep_duration = std::max(sleep_duration, t);
        }));
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
  audio_renderer_[0]->Play(
      play_at, 0,
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
  audio_capturer_.events().OnPacketProduced = CompletionCallback(
      [this, &captured](fuchsia::media::StreamPacket packet) {
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
  CleanUpCapturer();
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
  testing::AddGlobalTestEnvironment(
      new media::audio::test::AudioLoopbackEnvironment);

  int result = RUN_ALL_TESTS();

  return result;
}
