// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_HARDWARE_AUDIO_CORE_HARDWARE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_HARDWARE_AUDIO_CORE_HARDWARE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>

#include <unordered_set>

#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

class AudioCoreHardwareTest : public TestFixture {
 protected:
  static constexpr float kStreamGainDb = 0.0f;
  static constexpr float kUsageGainDb = 0.0f;
  static constexpr float kDeviceGainDb = 0.0f;

  static constexpr fuchsia::media::AudioCaptureUsage kUsage =
      fuchsia::media::AudioCaptureUsage::FOREGROUND;

  static constexpr fuchsia::media::AudioGainValidFlags kSetGainFlags =
      fuchsia::media::AudioGainValidFlags::GAIN_VALID &
      fuchsia::media::AudioGainValidFlags::MUTE_VALID;
  static constexpr fuchsia::media::AudioGainInfo kDeviceGain{.gain_db = kDeviceGainDb, .flags = {}};

  // We'll use just one payload buffer here.
  static constexpr uint32_t kPayloadBufferId = 0;
  static constexpr uint32_t kBufferDurationMsec = 500;

  static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
      fuchsia::media::AudioSampleFormat::FLOAT;
  static constexpr uint32_t kBytesPerSample = 4;
  static constexpr int32_t kNumChannels = 2;
  static constexpr int32_t kFrameRate = 48000;

  void SetUp() override;

  void ConnectToAudioCore();
  void ConnectToAudioCapturer();

  void WaitForCaptureDevice();
  void FailOnDeviceAddRemoveDefaultEvent();

  void SetCapturerFormat();
  void MapMemoryForCapturer();

  void ConnectToStreamGainControl();
  void SetGainsToUnity();

  void OnPacketProduced(fuchsia::media::StreamPacket packet);
  void DisplayReceivedAudio();

  std::ostringstream AllZeroesWarning();

  void set_audio_device_enumerator(fuchsia::media::AudioDeviceEnumeratorPtr audio_device_enum) {
    audio_device_enumerator_ = std::move(audio_device_enum);
  }
  fuchsia::media::AudioDeviceEnumeratorPtr& audio_device_enumerator() {
    return audio_device_enumerator_;
  }
  void set_audio(fuchsia::media::AudioPtr audio) { audio_ = std::move(audio); }
  fuchsia::media::AudioPtr& audio() { return audio_; }
  fuchsia::media::AudioCapturerPtr& audio_capturer() { return audio_capturer_; }
  fuchsia::media::audio::GainControlPtr& stream_gain_control() { return stream_gain_control_; }

  std::unordered_set<uint64_t> capture_device_tokens_;
  std::optional<uint64_t> default_capture_device_token() { return default_capture_device_token_; }
  void set_default_capture_device_token(std::optional<uint64_t> token) {
    default_capture_device_token_ = token;
  }
  static float ScaleToDb(float scale_val) { return std::log10(std::abs(scale_val)) * 20.0f; }

  fzl::VmoMapper payload_buffer_map_;
  float* payload_buffer_ = nullptr;

  uint32_t vmo_buffer_frame_count_;
  uint32_t vmo_buffer_byte_count_;

  int64_t received_payload_frames_ = 0;

 private:
  fuchsia::media::AudioDeviceEnumeratorPtr audio_device_enumerator_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
  fuchsia::media::audio::GainControlPtr stream_gain_control_;

  std::optional<uint64_t> default_capture_device_token_ = std::nullopt;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_HARDWARE_AUDIO_CORE_HARDWARE_TEST_H_
