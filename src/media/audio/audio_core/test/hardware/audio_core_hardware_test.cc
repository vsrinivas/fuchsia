// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/test/hardware/audio_core_hardware_test.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <cmath>
#include <optional>
#include <sstream>

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
  set_audio_device_enumerator(sys::ServiceDirectory::CreateFromNamespace()
                                  ->Connect<fuchsia::media::AudioDeviceEnumerator>());

  AddErrorHandler(audio_device_enumerator(), "AudioDeviceEnumerator");

  audio_device_enumerator().events().OnDeviceAdded =
      ([this](fuchsia::media::AudioDeviceInfo device) {
        ASSERT_NE(device.token_id, 0ul) << "Added device token cannot be 0";

        if (device.is_input) {
          capture_device_tokens_.insert(device.token_id);
          if (device.is_default) {
            set_default_capture_device_token(device.token_id);
          }
        }
      });

  audio_device_enumerator().events().OnDeviceRemoved = ([this](uint64_t token_id) {
    ASSERT_NE(token_id, 0ul) << "Removed device token cannot be 0";

    size_t num_removed = capture_device_tokens_.erase(token_id);
    if (default_capture_device_token() == token_id) {
      ASSERT_GT(num_removed, 0ul) << "Removed device was our default, but not in our set";
      set_default_capture_device_token(std::nullopt);
    }
  });

  audio_device_enumerator().events().OnDefaultDeviceChanged =
      ([this](uint64_t old_default_token, uint64_t new_default_token) {
        bool new_default_is_known = (capture_device_tokens_.count(new_default_token) > 0);
        bool old_default_is_known = (capture_device_tokens_.count(old_default_token) > 0);
        if (new_default_is_known) {
          set_default_capture_device_token(new_default_token);
        } else if (old_default_is_known && new_default_token == 0) {
          set_default_capture_device_token(std::nullopt);
        }
      });

  audio_device_enumerator()->GetDevices(
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
  audio_device_enumerator().events().OnDeviceAdded =
      ([](fuchsia::media::AudioDeviceInfo added_device) {
        if (added_device.is_input) {
          FAIL() << "Received OnDeviceAdded for an input device, during testing";
        } else {
          FX_LOGS(INFO) << "OnDeviceAdded for " << added_device.token_id << ", an output";
        }
      });

  audio_device_enumerator().events().OnDeviceRemoved = ([this](uint64_t removed_token_id) {
    if (default_capture_device_token().value() == removed_token_id) {
      FAIL() << "Received OnDeviceRemoved for our default capture device, during testing";
    } else {
      FX_LOGS(INFO) << "OnDeviceRemoved for " << removed_token_id
                    << ", a device other than our default capture device";
    }
  });

  audio_device_enumerator().events().OnDefaultDeviceChanged = ([this](
                                                                   uint64_t old_default_token_id,
                                                                   uint64_t new_default_token_id) {
    if (default_capture_device_token().value() == old_default_token_id) {
      FAIL()
          << "Received OnDefaultDeviceChanged AWAY from our default capture device, during testing";
    } else {
      FX_LOGS(INFO) << "OnDefaultDeviceChanged from " << old_default_token_id << " to "
                    << new_default_token_id << " (our default capture device is "
                    << default_capture_device_token().value() << ")";
    }
  });
}

void AudioCoreHardwareTest::ConnectToAudioCore() {
  set_audio(sys::ServiceDirectory::CreateFromNamespace()->Connect<fuchsia::media::Audio>());
  AddErrorHandler(audio(), "Audio");
}

void AudioCoreHardwareTest::ConnectToAudioCapturer() {
  ASSERT_TRUE(audio().is_bound());

  audio()->CreateAudioCapturer(audio_capturer().NewRequest(), false /* NOT loopback */);
  AddErrorHandler(audio_capturer(), "AudioCapturer");

  audio_capturer()->SetUsage(kUsage);
}

// Capture in a specific format, to minimize rate-conversion or rechannelization effects.
void AudioCoreHardwareTest::SetCapturerFormat() {
  audio_capturer()->SetPcmStreamType({
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

  audio_capturer()->AddPayloadBuffer(kPayloadBufferId, std::move(audio_capturer_vmo));

  payload_buffer_ = reinterpret_cast<float*>(payload_buffer_map_.start());
  ASSERT_NE(payload_buffer_, nullptr);
}

void AudioCoreHardwareTest::ConnectToStreamGainControl() {
  ASSERT_TRUE(audio_capturer().is_bound());

  audio_capturer()->BindGainControl(stream_gain_control().NewRequest());
  AddErrorHandler(stream_gain_control(), "AudioCapturer::GainControl");
}

// Set gain for this capturer gain control, capture usage and all capture devices.
void AudioCoreHardwareTest::SetGainsToUnity() {
  ASSERT_TRUE(stream_gain_control().is_bound());
  ASSERT_TRUE(audio_device_enumerator().is_bound());
  ASSERT_FALSE(capture_device_tokens_.empty());

  stream_gain_control()->SetGain(kStreamGainDb);

  for (auto token_id : capture_device_tokens_) {
    audio_device_enumerator()->SetDeviceGain(token_id, kDeviceGain, kSetGainFlags);
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

std::ostringstream AudioCoreHardwareTest::AllZeroesWarning() {
  std::ostringstream stream;
  stream << "Things to check:" << std::endl
         << " - Is a Mic-Mute switch on?" << std::endl
         << " - Did the audio driver set the input HW sensitivity too low?" << std::endl
         << " - Is this a DIGITAL audio input?" << std::endl
         << " - Is a VirtualAudioDevice the default audio input?";
  return stream;
}

// This test validates the real-world end-to-end data path -- from built-in microphone, through
// analog and digital hardware paths configured by the audio driver, to audio services (device
// management, processing) via an input ring buffer, to an audio client via the AudioCapturer API.
//
// The test does this by opening an input stream and ensuring that the captured audio contains
// expected values for real-world analog hardware.
//
// When capturing from the real built-in microphone, the analog noise floor ensures that there
// should be at least 1 bit of ongoing broad-spectrum signal (excluding professional-grade
// products). Thus, if we are accurately capturing the analog noise floor, a span of received
// 0.0 might be common, but certainly not the entire buffer. However, if our timing calculations are
// incorrect, or if the audio hardware has been incorrectly initialized and input DMA is not
// operating, then the entire capture buffer might contain audio samples with value '0.0'.
//
// We use a standard format/frame_rate to minimize frame-rate-conversion or rechannelization,
// checking that we receive at least 2 distinct values (which implies at least 1 non-'0.0' value).
TEST_F(AudioCoreHardwareTest, AnalogNoiseDetectable) {
  const uint32_t payload_offset = 0u;

  audio_capturer()->CaptureAt(kPayloadBufferId, payload_offset, vmo_buffer_frame_count_,
                              AddCallback("CaptureAt", [this](fuchsia::media::StreamPacket packet) {
                                OnPacketProduced(packet);
                              }));
  // Wait for the capture buffer to be returned.
  ExpectCallbacks();
  ASSERT_GT(received_payload_frames_, 0) << "No data frames captured";
  ASSERT_GT(received_payload_frames_ * kNumChannels, 1) << "Insufficient data for comparison";

  float sum_squares = 0.0f;
  bool all_values_equal = true;
  for (auto idx = 0u; idx < received_payload_frames_ * kNumChannels; ++idx) {
    sum_squares += (payload_buffer_[idx] * payload_buffer_[idx]);
    all_values_equal = all_values_equal && (payload_buffer_[idx] != payload_buffer_[0]);
  }
  ASSERT_GT(sum_squares, 0.0f) << "Captured signal is all zeroes. " << AllZeroesWarning().str();
  ASSERT_FALSE(all_values_equal) << "Captured signal is purely constant (" << payload_buffer_[0]
                                 << "). " << AllZeroesWarning().str();

  float rms = std::sqrt(sum_squares / static_cast<float>(received_payload_frames_));
  FX_LOGS(INFO) << "Across " << received_payload_frames_ << " frames, we measured " << std::fixed
                << std::setprecision(2) << ScaleToDb(rms) << " dBfs RMS of ambient noise";
}

// Previous test ensures that the end-to-end audio input path captures appropriate analog values.
//
// This test in turn validates that the audio system -- and default audio input __hardware__ -- do
// not inject an unacceptable level of constant offset (which causes problems for capture clients).
//
// This test will fail when run on audio hardware that is out-of-calibration or has degraded over
// time. Such a device would not fare well in real-world audio usage, but could still successfully
// run all tests that do not involve the analog realm. Accordingly, this case is DISABLED in CQ, but
// retained so it is available for engineering desktop (and perhaps a future `ffx audio hw-check` or
// other hardware diagnostic).
TEST_F(AudioCoreHardwareTest, DISABLED_MinimalDcOffset) {
  const uint32_t payload_offset = 0u;

  audio_capturer()->CaptureAt(kPayloadBufferId, payload_offset, vmo_buffer_frame_count_,
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
    GTEST_SKIP() << "Captured signal is all zeroes. DC offset cannot be measured. "
                 << AllZeroesWarning().str();
  }

  float mean = sum / static_cast<float>(received_payload_frames_);
  float sum_square_diffs = 0.0;
  for (auto idx = 0u; idx < received_payload_frames_ * kNumChannels; ++idx) {
    float diff = payload_buffer_[idx] - mean;
    sum_square_diffs += (diff * diff);
  }
  ASSERT_GT(sum_square_diffs, 0.0f) << "Captured signal is purely constant (" << payload_buffer_[0]
                                    << "). " << AllZeroesWarning().str();

  float std_dev = std::sqrt(sum_square_diffs / static_cast<float>(received_payload_frames_));
  EXPECT_TRUE(std_dev > std::abs(mean))
      << std::endl
      << "***** The audio input device has a detectable " << (mean > 0.0f ? "positive" : "negative")
      << " DC Offset bias. Standard deviation (" << std::fixed << std::setprecision(7) << std_dev
      << ") should exceed absolute mean (" << mean << ", approx " << std::setprecision(2)
      << ScaleToDb(std::abs(mean)) << " dBFS)." << std::endl
      << "***** This device should probably be retired, since DC Offset can cause malfunctions for "
      << "services that process the audio input stream, such as speech recognition or WebRTC.";

  FX_LOGS(INFO) << "Captured " << received_payload_frames_ << " frames; mean value: " << std::fixed
                << std::setprecision(7) << mean << " (" << std::setprecision(2)
                << ScaleToDb(std::abs(mean)) << " dBFS); stddev: " << std::setprecision(7)
                << std_dev << " (" << std::setprecision(2) << std_dev / std::abs(mean)
                << " x Mean).";
}

}  // namespace media::audio::test
