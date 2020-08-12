// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/test/hardware/audio_core_hardware_test.h"

#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

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
  ASSERT_TRUE(WaitForCaptureDevice());
  ConnectToAudioCapturer();

  ConnectToGainControl();
  SetGainsToUnity();

  GetDefaultCaptureFormat();
  SetCapturerFormat();

  MapMemoryForCapturer();
  RunLoopUntilIdle();
}

bool AudioCoreHardwareTest::WaitForCaptureDevice() {
  audio_device_enumerator_ = sys::ServiceDirectory::CreateFromNamespace()
                                 ->Connect<fuchsia::media::AudioDeviceEnumerator>();

  AddErrorHandler(audio_device_enumerator_, "AudioDeviceEnumerator");

  audio_device_enumerator_.events().OnDeviceAdded =
      ([this](fuchsia::media::AudioDeviceInfo device) {
        if (device.is_input) {
          capture_device_tokens_.insert(device.token_id);
          if (device.is_default) {
            capture_device_is_default_ = true;
          }
        }
      });

  audio_device_enumerator_.events().OnDeviceRemoved = ([this](uint64_t token_id) {
    size_t num_removed = capture_device_tokens_.erase(token_id);
    FAIL() << "OnDeviceRemoved: " << num_removed << " input devices just departed";
  });

  audio_device_enumerator_.events().OnDefaultDeviceChanged =
      ([this](uint64_t old_default_token, uint64_t new_default_token) {
        if (capture_device_tokens_.count(new_default_token) > 0) {
          capture_device_is_default_ = true;
        } else if (capture_device_tokens_.count(old_default_token) > 0 && new_default_token == 0) {
          capture_device_is_default_ = false;
          FAIL() << "OnDefaultDeviceChanged: " << old_default_token
                 << " is no longer default input (now 0)";
        }
      });

  audio_device_enumerator_->GetDevices(
      [this](std::vector<fuchsia::media::AudioDeviceInfo> devices) {
        for (auto& device : devices) {
          if (device.is_input) {
            capture_device_tokens_.insert(device.token_id);
            if (device.is_default) {
              capture_device_is_default_ = true;
            }
          }
        }
      });

  RunLoopWithTimeoutOrUntil([this]() { return ErrorOccurred() || capture_device_is_default_; },
                            kDurationResponseExpected);
  return capture_device_is_default_;
}

void AudioCoreHardwareTest::ConnectToAudioCore() {
  audio_core_ = sys::ServiceDirectory::CreateFromNamespace()->Connect<fuchsia::media::AudioCore>();
  AddErrorHandler(audio_core_, "AudioCore");
}

void AudioCoreHardwareTest::ConnectToAudioCapturer() {
  ASSERT_TRUE(audio_core_.is_bound());

  constexpr bool kNotLoopback = false;
  audio_core_->CreateAudioCapturer(kNotLoopback, audio_capturer_.NewRequest());
  AddErrorHandler(audio_capturer_, "AudioCapturer");

  audio_capturer_->SetUsage(kUsage);
}

void AudioCoreHardwareTest::ConnectToGainControl() {
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

// Fetch the initial media type and adjust channel_count_ and frames_per_second_ if needed.
void AudioCoreHardwareTest::GetDefaultCaptureFormat() {
  audio_capturer_->GetStreamType(
      AddCallback("GetStreamType", [this](fuchsia::media::StreamType stream_type) {
        ASSERT_TRUE(stream_type.medium_specific.is_audio()) << "Default format is not audio!";
        const auto& format = stream_type.medium_specific.audio();

        channel_count_ = format.channels;
        frames_per_second_ = format.frames_per_second;
      }));

  ExpectCallback();

  vmo_buffer_frame_count_ = (kBufferDurationMsec * frames_per_second_) / 1000;
  vmo_buffer_byte_count_ = vmo_buffer_frame_count_ * channel_count_ * kBytesPerSample;
}

// Capture in the input's default format, to minimize rate-conversion or rechannelization effects.
void AudioCoreHardwareTest::SetCapturerFormat() {
  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = kSampleFormat;
  audio_stream_type.channels = channel_count_;
  audio_stream_type.frames_per_second = frames_per_second_;

  audio_capturer_->SetPcmStreamType(audio_stream_type);
}

// Create a shared payload buffer, map it into our process, duplicate the VMO handle and pass it to
// the capturer as a payload buffer.
void AudioCoreHardwareTest::MapMemoryForCapturer() {
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
}

// A packet containing captured audio data was just returned to us -- handle it.
void AudioCoreHardwareTest::OnPacketProduced(fuchsia::media::StreamPacket pkt) {
  received_payload_frames_ = pkt.payload_size / (channel_count_ * kBytesPerSample);

  EXPECT_EQ(pkt.payload_offset, 0u);
  EXPECT_EQ(pkt.payload_size, vmo_buffer_byte_count_);
}

// Used when debugging repeatable test failures
void AudioCoreHardwareTest::DisplayReceivedAudio() {
  ASSERT_NE(payload_buffer_, nullptr);

  for (auto idx = 0u; idx < received_payload_frames_ * channel_count_; ++idx) {
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
// Note that we do this at the audio input device's native (default) frame_rate and channel_count,
// to minimize any loss in transparency from frame-rate-conversion or rechannelization.
TEST_F(AudioCoreHardwareTest, ZeroesInLiveCapture) {
  const uint32_t payload_offset = 0u;

  audio_capturer_->CaptureAt(kPayloadBufferId, payload_offset, vmo_buffer_frame_count_,
                             AddCallback("CaptureAt", [this](fuchsia::media::StreamPacket packet) {
                               OnPacketProduced(packet);
                             }));
  // Wait for the capture buffer to be returned.
  ExpectCallback();

  bool found_nonzero_value = false;

  ASSERT_NE(payload_buffer_, nullptr);
  for (auto idx = 0u; idx < received_payload_frames_ * channel_count_; ++idx) {
    if (payload_buffer_[idx] != 0.0f) {
      found_nonzero_value = true;
      break;
    }
  }

  EXPECT_TRUE(found_nonzero_value) << "Mic mute? HW sensitivity too low? Digital input? VAD?";
}

// TODO(mpuryear): add test case to detect DC offset, using variance from the average value.

}  // namespace media::audio::test
