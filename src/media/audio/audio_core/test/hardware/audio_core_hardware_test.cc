// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/test/hardware/audio_core_hardware_test.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <cmath>
#include <optional>

// TODO(fxbug.dev/49807): This test should automatically fail if underflows are detected. That
// functionality should be ported from HermeticAudioTest to here.

namespace media::audio::test {

// For operations expected to generate a response, wait __1 minute__. We do this to avoid flaky
// results when testing on high-load (high-latency) environments. For reference, in mid-2018 when
// observing highly-loaded local QEMU instances running code that generated correct completion
// responses, we observed timeouts if waiting 20 ms, but not if waiting 50 ms. This value is 3000x
// that (!) -- WELL beyond the limit of human acceptability. Thus, intermittent failures (rather
// than being a "potentially flaky test") mean that the system is, intermittently, UNACCEPTABLE.
constexpr zx::duration kDurationResponseExpected = zx::sec(60);

void AudioCoreHardwareTest::SetUp() {
  TestFixture::SetUp();

  ConnectToAudioCore();
  WaitForCaptureDevice();
  ConnectToAudioCapturer();

  SetCapturerFormat();
  MapMemoryForCapturer();

  FailOnDeviceAddRemoveDefaultEvent();
  ASSERT_TRUE(default_capture_device_token().has_value());

  ConnectToStreamGainControl();
  SetGainsToUnity();

  RunLoopUntilIdle();
}

void AudioCoreHardwareTest::WaitForCaptureDevice() {
  audio_device_enumerator_ = sys::ServiceDirectory::CreateFromNamespace()
                                 ->Connect<fuchsia::media::AudioDeviceEnumerator>();

  AddErrorHandler(audio_device_enumerator_, "AudioDeviceEnumerator");

  audio_device_enumerator_.events().OnDeviceAdded =
      ([this](fuchsia::media::AudioDeviceInfo device) {
        ASSERT_NE(device.token_id, 0ul) << "Added device token cannot be 0";

        if (device.is_input) {
          capture_device_tokens_.insert(device.token_id);
          if (device.is_default) {
            set_default_capture_device_token(device.token_id);
          }
        }
      });

  audio_device_enumerator_.events().OnDeviceRemoved = ([this](uint64_t token_id) {
    ASSERT_NE(token_id, 0ul) << "Removed device token cannot be 0";

    size_t num_removed = capture_device_tokens_.erase(token_id);
    if (default_capture_device_token() == token_id) {
      ASSERT_GT(num_removed, 0ul) << "Removed device was our default, but not in our set";
      set_default_capture_device_token(std::nullopt);
    }
  });

  audio_device_enumerator_.events().OnDefaultDeviceChanged =
      ([this](uint64_t old_default_token, uint64_t new_default_token) {
        bool new_default_is_known = (capture_device_tokens_.count(new_default_token) > 0);
        bool old_default_is_known = (capture_device_tokens_.count(old_default_token) > 0);
        if (new_default_is_known) {
          set_default_capture_device_token(new_default_token);
        } else if (old_default_is_known && new_default_token == 0) {
          set_default_capture_device_token(std::nullopt);
        }
      });

  audio_device_enumerator_->GetDevices(
      [this](std::vector<fuchsia::media::AudioDeviceInfo> devices) {
        for (auto& device : devices) {
          if (device.is_input) {
            capture_device_tokens_.insert(device.token_id);
            if (device.is_default) {
              set_default_capture_device_token(device.token_id);
            }
          }
        }
      });

  RunLoopWithTimeoutOrUntil(
      [this]() { return ErrorOccurred() || default_capture_device_token().has_value(); },
      kDurationResponseExpected);
}

void AudioCoreHardwareTest::FailOnDeviceAddRemoveDefaultEvent() {
  audio_device_enumerator_.events().OnDeviceAdded =
      ([](fuchsia::media::AudioDeviceInfo added_device) {
        FAIL() << "Received OnDeviceAdded during testing";
      });

  audio_device_enumerator_.events().OnDeviceRemoved =
      ([](uint64_t removed_token_id) { FAIL() << "Received OnDeviceRemoved during testing"; });

  audio_device_enumerator_.events().OnDefaultDeviceChanged =
      ([](uint64_t old_default_token_id, uint64_t new_default_token_id) {
        FAIL() << "Received OnDefaultDeviceChanged during testing";
      });
}

void AudioCoreHardwareTest::ConnectToAudioCore() {
  audio_core_ = sys::ServiceDirectory::CreateFromNamespace()->Connect<fuchsia::media::AudioCore>();
  AddErrorHandler(audio_core_, "AudioCore");
}

void AudioCoreHardwareTest::ConnectToAudioCapturer() {
  ASSERT_TRUE(audio_core_.is_bound());

  audio_core_->CreateAudioCapturer(false /* NOT loopback */, audio_capturer_.NewRequest());
  AddErrorHandler(audio_capturer_, "AudioCapturer");

  audio_capturer_->SetUsage(kUsage);
}

// Capture in a specific format, to minimize rate-conversion or rechannelization effects.
void AudioCoreHardwareTest::SetCapturerFormat() {
  audio_capturer_->SetPcmStreamType({
      .sample_format = kSampleFormat,
      .channels = kNumChannels,
      .frames_per_second = kFrameRate,
  });
}

// Create a shared payload buffer, map it into our process, duplicate the VMO handle and pass it to
// the capturer as a payload buffer.
void AudioCoreHardwareTest::MapMemoryForCapturer() {
  vmo_buffer_frame_count_ = (kBufferDurationMsec * kFrameRate) / 1000;
  vmo_buffer_byte_count_ = vmo_buffer_frame_count_ * kNumChannels * kBytesPerSample;

  zx::vmo audio_capturer_vmo;
  constexpr zx_vm_option_t kMapOptions = ZX_VM_PERM_READ;
  constexpr zx_rights_t kVmoRights =
      ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;

  zx_status_t status = payload_buffer_map_.CreateAndMap(vmo_buffer_byte_count_, kMapOptions,
                                                        /* vmar_manager= */ nullptr,
                                                        &audio_capturer_vmo, kVmoRights);
  EXPECT_EQ(status, ZX_OK) << "VmoMapper::CreateAndMap failed: " << zx_status_get_string(status)
                           << " (" << status << ")";

  audio_capturer_->AddPayloadBuffer(kPayloadBufferId, std::move(audio_capturer_vmo));

  payload_buffer_ = reinterpret_cast<float*>(payload_buffer_map_.start());
  ASSERT_NE(payload_buffer_, nullptr);
}

void AudioCoreHardwareTest::ConnectToStreamGainControl() {
  ASSERT_TRUE(audio_capturer_.is_bound());

  audio_capturer_->BindGainControl(stream_gain_control_.NewRequest());
  AddErrorHandler(stream_gain_control_, "AudioCapturer::GainControl");
}

// Set gain for this capturer gain control, capture usage and all capture devices.
void AudioCoreHardwareTest::SetGainsToUnity() {
  ASSERT_TRUE(stream_gain_control_.is_bound());
  ASSERT_TRUE(audio_device_enumerator_.is_bound());
  ASSERT_FALSE(capture_device_tokens_.empty());

  stream_gain_control_->SetGain(kStreamGainDb);
  audio_core_->SetCaptureUsageGain(kUsage, kUsageGainDb);

  for (auto token_id : capture_device_tokens_) {
    audio_device_enumerator_->SetDeviceGain(token_id, kDeviceGain, kSetGainFlags);
  }
}

// A packet containing captured audio data was just returned to us -- handle it.
void AudioCoreHardwareTest::OnPacketProduced(fuchsia::media::StreamPacket pkt) {
  received_payload_frames_ =
      static_cast<int64_t>(pkt.payload_size) / static_cast<int64_t>(kNumChannels * kBytesPerSample);

  EXPECT_EQ(pkt.payload_offset, 0u);
  EXPECT_EQ(pkt.payload_size, vmo_buffer_byte_count_);
}

// Used when debugging repeatable test failures
void AudioCoreHardwareTest::DisplayReceivedAudio() {
  for (auto idx = 0u; idx < received_payload_frames_ * kNumChannels; ++idx) {
    if (idx % 16 == 0) {
      printf("\n[%3x]", idx);
    }
    printf(" %8.05f", payload_buffer_[idx]);
  }
  printf("\n");
}

// When capturing from the real built-in microphone, the analog noise floor ensures that there
// should be at least 1 bit of ongoing broad-spectrum signal (excluding professional-grade
// products). Thus, if we are accurately capturing the analog noise floor, a span of received
// 0.0 might be common, but certainly not the entire buffer. However, if our timing calculations are
// incorrect, or if the audio hardware has been incorrectly initialized and input DMA is not
// operating, then the entire capture buffer might contain audio samples with value '0.0'.
//
// To validate the hardware initialization and our input pipeline (at a VERY coarse level), we
// record a buffer from the live audio input, checking that we receive at least 1 non-'0.0' value.
//
// We use a standard format frame_rate, to minimize frame-rate-conversion or rechannelization.
TEST_F(AudioCoreHardwareTest, AnalogNoiseDetectable) {
  const uint32_t payload_offset = 0u;

  audio_capturer_->CaptureAt(kPayloadBufferId, payload_offset, vmo_buffer_frame_count_,
                             AddCallback("CaptureAt", [this](fuchsia::media::StreamPacket packet) {
                               OnPacketProduced(packet);
                             }));
  // Wait for the capture buffer to be returned.
  ExpectCallbacks();
  ASSERT_GT(received_payload_frames_, 0) << "No data frames captured";

  float sum_squares = 0.0f;
  for (auto idx = 0u; idx < received_payload_frames_ * kNumChannels; ++idx) {
    sum_squares += (payload_buffer_[idx] * payload_buffer_[idx]);
  }
  ASSERT_GT(sum_squares, 0.0f) << "Captured signal is all zeroes. "
                               << "Things to check: Is Mic-Mute switch on? "
                               << "Did audio driver set input HW sensitivity too low? "
                               << "Is this a digital input? "
                               << "Is VirtualAudioDevice the default input?";

  float rms = std::sqrt(sum_squares / static_cast<float>(received_payload_frames_));
  FX_LOGS(INFO) << "Across " << received_payload_frames_ << " frames, we measured " << std::fixed
                << std::setprecision(2) << ScaleToDb(rms) << " dB RMS of ambient noise";
}

TEST_F(AudioCoreHardwareTest, MinimalDcOffset) {
  const uint32_t payload_offset = 0u;

  audio_capturer_->CaptureAt(kPayloadBufferId, payload_offset, vmo_buffer_frame_count_,
                             AddCallback("CaptureAt", [this](fuchsia::media::StreamPacket packet) {
                               OnPacketProduced(packet);
                             }));
  ExpectCallbacks();
  ASSERT_GT(received_payload_frames_, 0) << "No data frames captured";

  float sum = 0.0f;
  bool nonzero_sample_found = false;
  for (auto idx = 0u; idx < received_payload_frames_ * kNumChannels; ++idx) {
    sum += payload_buffer_[idx];
    nonzero_sample_found = nonzero_sample_found || (payload_buffer_[idx] != 0.0f);
  }
  if (!nonzero_sample_found) {
    GTEST_SKIP() << "Captured signal is all zeroes. DC offset cannot be measured";
  }

  float mean = sum / static_cast<float>(received_payload_frames_);
  float sum_square_diffs = 0.0;
  for (auto idx = 0u; idx < received_payload_frames_ * kNumChannels; ++idx) {
    float diff = payload_buffer_[idx] - mean;
    sum_square_diffs += (diff * diff);
  }
  EXPECT_GT(sum_square_diffs, 0.0f) << "Captured signal is purely constant";

  float std_dev = std::sqrt(sum_square_diffs / static_cast<float>(received_payload_frames_));
  EXPECT_TRUE(std_dev > std::abs(mean))
      << "This device has a detectable " << (mean > 0.0f ? "positive" : "negative")
      << " DC Offset. Standard deviation (" << std::fixed << std::setprecision(7) << std_dev
      << ") should exceed the absolute mean (" << mean << ", approx " << std::setprecision(2)
      << ScaleToDb(std::abs(mean)) << " dB)";
}

}  // namespace media::audio::test
