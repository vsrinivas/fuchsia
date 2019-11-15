// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_HARDWARE_AUDIO_CORE_HARDWARE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_HARDWARE_AUDIO_CORE_HARDWARE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>

#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

class AudioCoreHardwareTest : public TestFixture {
 protected:
  // Once this test is shown to be flake-free in our test environment, lower this to perhaps 2.
  // If gain is full, and if we are capturing from a real microphone in a normal acoustic
  // environment (not an anechoic enclosure), then 2 frames is a very reasonable limit.
  static constexpr uint32_t kLimitConsecFramesZero = 5;

  // We'll use just one payload buffer here.
  static constexpr uint32_t kPayloadBufferId = 0;
  static constexpr uint32_t kBufferDurationMsec = 1000;

  static constexpr uint32_t kDefaultFramesPerSecond = 16000;
  static constexpr uint32_t kDefaultChannelCount = 2;

  static constexpr fuchsia::media::AudioSampleFormat kSampleFormat =
      fuchsia::media::AudioSampleFormat::FLOAT;
  static constexpr uint32_t kBytesPerSample = 4;

  void SetUp() override;
  void TearDown() override;

  void ConnectToAudioCore();
  void ConnectToAudioCapturer();

  void GetDefaultCaptureFormat();
  void SetCapturerFormat();

  void MapMemoryForCapturer();

  void OnPacketProduced(fuchsia::media::StreamPacket packet);
  void DisplayReceivedAudio();

  fuchsia::media::AudioCorePtr audio_core_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;

  uint32_t channel_count_ = kDefaultChannelCount;
  uint32_t frames_per_second_ = kDefaultFramesPerSecond;

  fzl::VmoMapper payload_buffer_map_;
  float* payload_buffer_ = nullptr;

  uint32_t vmo_buffer_frame_count_;
  uint32_t vmo_buffer_byte_count_;

  uint32_t received_payload_frames_ = 0;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_HARDWARE_AUDIO_CORE_HARDWARE_TEST_H_
