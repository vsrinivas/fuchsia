// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_AUDIO_PIPELINE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_AUDIO_PIPELINE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <set>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

// VAD format values
constexpr uint32_t kFrameRate = 48000;
constexpr uint16_t kRateFamilyFlags = 1;  // CONTINUOUS

constexpr uint32_t kSampleFormat = 4;  // 16-bit LPCM
constexpr auto kAudioFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
constexpr uint32_t kSampleSize = 2;

constexpr uint32_t kNumChannels = 2;
constexpr uint32_t kFrameSize = kSampleSize * kNumChannels;

constexpr uint32_t kFifoDepthBytes = 0;
constexpr auto kExternalDelay = zx::msec(0);

// Test-specific values
// For our shared buffer to the renderer, use 50 pkts of 10 ms each
constexpr uint32_t kPacketMs = 10;
constexpr uint32_t kNumPayloads = 50;
constexpr uint32_t kPacketFrames = kFrameRate / 1000 * kPacketMs;
constexpr uint32_t kPacketSamples = kPacketFrames * kNumChannels;
constexpr uint32_t kPacketBytes = kPacketSamples * kSampleSize;
constexpr uint32_t kRendererFrames = kPacketFrames * kNumPayloads;
constexpr uint32_t kRendererBytes = kFrameSize * kRendererFrames;

// Set VAD ring buffer to 1000 ms, with notifs every 10ms
constexpr uint32_t kSectionMs = 10;
constexpr uint32_t kNumRingSections = 100;
constexpr uint32_t kSectionFrames = kFrameRate / 1000 * kSectionMs;
constexpr uint32_t kSectionBytes = kFrameSize * kSectionFrames;
constexpr uint32_t kRingFrames = kNumRingSections * kSectionFrames;
constexpr uint32_t kRingBytes = kFrameSize * kRingFrames;

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

  uint64_t RingBufferSize() const { return kFrameSize * num_rb_frames_; }
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
  void DisplaySnapshotBuffer();

  void MapAndAddRendererBuffer(uint32_t buffer_id);

  // Send timestamped packets with values increasing by 1 per sample. By default, first sample is 1
  // for packet PTS 0. By default we send an extra packet of silence, to work around fxb/49980.
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  void CreateAndSendPackets(uint32_t num_packets, int16_t initial_data_value = 1,
                            int64_t initial_pts = 0, bool final_silent_packet = true);

  // Wait for packet with the given ID to complete.
  void WaitForPacket(uint32_t packet_num);

  // Call Play(), perfectly aligned with the VAD ring buffer rollover.
  void SynchronizedPlay();

  //
  // virtualaudio-related members
  static fuchsia::virtualaudio::ControlSyncPtr control_sync_;
  fuchsia::virtualaudio::OutputPtr output_;

  bool received_set_format_ = false;
  bool received_set_gain_ = false;
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
  bool received_remove_device_ = false;
  bool received_gain_changed_ = false;
  bool received_default_output_changed_ = false;

  uint64_t device_token_ = 0;
  bool device_is_default_ = false;
  bool device_mute_ = true;
  float device_gain_db_ = fuchsia::media::audio::MUTED_GAIN_DB;

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
