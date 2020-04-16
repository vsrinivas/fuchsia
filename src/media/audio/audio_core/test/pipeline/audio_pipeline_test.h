// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_AUDIO_PIPELINE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_AUDIO_PIPELINE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <unordered_set>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// VAD default format values
constexpr uint32_t kDefaultFrameRate = 48000;
constexpr uint32_t kDefaultSampleFormat = 4;  //  16-bit LPCM
constexpr fuchsia::media::AudioSampleFormat kDefaultAudioFormat =
    fuchsia::media::AudioSampleFormat::SIGNED_16;
constexpr uint32_t kDefaultSampleSize = 2;
constexpr uint32_t kDefaultNumChannels = 2;
constexpr zx_duration_t kDefaultExternalDelayNs = 0;
constexpr uint32_t kDefaultFrameSize = kDefaultSampleSize * kDefaultNumChannels;

// Test-specific values
// For our shared buffer to the renderer, use 50 pkts of 10 ms each
constexpr uint32_t kPacketMs = 10;
constexpr uint32_t kNumPayloads = 50;
constexpr uint32_t kPacketFrames = kDefaultFrameRate / 1000 * kPacketMs;
constexpr uint32_t kPacketBytes = kDefaultFrameSize * kPacketFrames;
constexpr uint32_t kRendererFrames = kPacketFrames * kNumPayloads;
constexpr uint32_t kRendererBytes = kDefaultFrameSize * kRendererFrames;

// Set VAD ring buffer to 1000 ms, with notifs every 10ms
constexpr uint32_t kSectionMs = 10;
constexpr uint32_t kNumRingSections = 100;
constexpr uint32_t kSectionFrames = kDefaultFrameRate / 1000 * kSectionMs;
constexpr uint32_t kSectionBytes = kDefaultFrameSize * kSectionFrames;
constexpr uint32_t kRingFrames = kNumRingSections * kSectionFrames;
constexpr uint32_t kRingBytes = kDefaultFrameSize * kRingFrames;

class AudioPipelineTest : public HermeticAudioCoreTest {
 protected:
  static void SetUpTestSuite();
  static void TearDownTestSuite();
  void SetUp() override;
  void TearDown() override;

  void AddVirtualOutput();
  void SetVirtualAudioEvents();
  void ResetVirtualAudioEvents();
  void WaitForVirtualDeviceDepartures();

  void SetAudioDeviceEvents();
  void ResetAudioDeviceEvents();

  void SetUpRenderer();
  void SetAudioRendererEvents();
  void ResetAudioRendererEvents();

  void SetUpBuffers();

  uint64_t RingBufferSize() const { return kDefaultFrameSize * num_rb_frames_; }
  uint8_t* RingBufferStart() const { return reinterpret_cast<uint8_t*>(ring_buffer_.start()); }

  // Make a copy of the virtual audio device's ring buffer, into our "snapshot" buffer.
  void CreateSnapshotOfRingBuffer();

  // Starting at the frame specified, return the next subsequent frame number that is zero/non-zero.
  // This is used in determining whether the audio system behaved as expected.
  uint32_t NextContiguousSnapshotFrame(bool look_for_nonzero, uint32_t start_frame);

  // Upon failure, we display portions of the snapshot buffer, for debugging purposes.
  void DisplaySnapshotSection(uint32_t section);
  void DisplaySnapshotSectionsForFrames(uint32_t first = 0, uint32_t second = 0, uint32_t third = 0,
                                        uint32_t fourth = 0, uint32_t fifth = 0);

  void MapAndAddRendererBuffer(uint32_t buffer_id);

  // Submit timestamped packets (also with ID-tag starting with 0). Payload data vals increase by 1
  // per sample. By default, val of first payload sample is 1, and first packet PTS is 0.
  void CreateAndSendPackets(uint32_t num_packets, int16_t initial_data_value = 1,
                            int64_t initial_pts = 0);

  // Wait for packet with the given ID to complete.
  void WaitForPacket(uint32_t packet_num);

  // Call Play(), perfectly aligned with the VAD ring buffer rollover.
  void SynchronizedPlay();

  //
  // virtualaudio-related members
  static fuchsia::virtualaudio::ControlSyncPtr control_sync_;
  fuchsia::virtualaudio::OutputPtr output_;
  fuchsia::virtualaudio::InputPtr input_;
  std::unordered_set<uint64_t> virtual_device_tokens_;

  bool received_set_format_ = false;
  bool received_set_gain_ = false;
  float gain_db_ = fuchsia::media::audio::MUTED_GAIN_DB;
  bool received_ring_buffer_ = false;
  zx::vmo rb_vmo_;
  uint32_t num_rb_frames_ = 0;
  fzl::VmoMapper ring_buffer_;
  bool received_start_ = false;
  zx_time_t start_time_ = 0;
  bool received_stop_ = false;
  zx_time_t stop_time_ = 0;
  uint32_t stop_pos_ = 0;
  bool received_discard_all_ = false;
  uint32_t ring_pos_ = 0;
  uint64_t running_ring_pos_ = 0;
  zx_time_t latest_pos_notify_time_ = 0;

  // Snapshot of ring buffer, for comparison
  std::unique_ptr<uint8_t[]> compare_buff_;

  //
  // AudioDeviceEnum-related members
  fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum_;
  bool received_add_device_ = false;
  uint64_t received_add_device_token_ = 0;
  float received_gain_db_ = fuchsia::media::audio::MUTED_GAIN_DB;
  bool received_mute_ = true;
  bool received_remove_device_ = false;
  bool received_gain_changed_ = false;
  bool received_default_device_changed_ = false;
  uint64_t received_default_device_token_ = 0;

  //
  // AudioRenderer-related members
  fuchsia::media::AudioRendererPtr audio_renderer_;

  bool received_min_lead_time_ = false;
  int64_t min_lead_time_ = -1;

  fzl::VmoMapper payload_buffer_;

  bool received_play_ = false;
  int64_t received_play_ref_time = 0;
  int64_t received_play_media_time_ = -1;

  bool received_pause_ = false;
  int64_t received_pause_ref_time_ = 0;
  int64_t received_pause_media_time_ = -1;

  bool received_packet_completion_ = false;
  uint32_t received_packet_num_ = 0;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_AUDIO_PIPELINE_TEST_H_
