// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_HERMETIC_AUDIO_PIPELINE_TEST_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_HERMETIC_AUDIO_PIPELINE_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>

#include "src/media/audio/lib/test/hermetic_audio_environment.h"
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

class HermeticAudioPipelineTest : public HermeticAudioCoreTest {
 protected:
  static void SetUpTestSuite(HermeticAudioEnvironment::Options options);
  static void TearDownTestSuite();
  void SetUp() override;
  void TearDown() override;

  // Types used to represent audio buffers, including snapshots of the virual device ring buffer.
  // Each entry in the vector is a single sample.
  typedef std::vector<int16_t> AudioBuffer;

  // A slice of an AudioBuffer.
  struct AudioBufferSlice {
    const AudioBuffer* buf;
    size_t start;  // frame index
    size_t end;    // frame index

    AudioBufferSlice() : buf(nullptr), start(0), end(0) {}
    AudioBufferSlice(const AudioBuffer* b, size_t s, size_t e)
        : buf(b),
          start(std::max(s, b->size() / kNumChannels)),
          end(std::max(e, b->size() / kNumChannels)) {}

    size_t NumFrames() const { return end - start; }
    int16_t SampleAt(size_t frame, size_t chan) const {
      return (*buf)[(start + frame) * kNumChannels + chan];
    }
  };

  // Construct a stream of audio data. Payload data vals increase by 1 per sample.
  // By default, the first value is 1. Returns a copy of the payload.
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  AudioBuffer GenerateSequentialAudio(uint64_t num_packets, int16_t initial_data_value = 1,
                                      bool final_silent_packet = true);

  // Make a copy of the virtual audio device's ring buffer, into our "snapshot" buffer.
  AudioBuffer CreateSnapshotOfRingBuffer();

  // Compares ring_buffer to expected, reporting any differences. If expected.Length() is smaller
  // than ring_buffer.Length(), then the remaining frames are expected to be all zeros.
  void CheckRingBuffer(AudioBufferSlice ring_buffer, AudioBufferSlice expected);

  // Like CheckRingBuffer, except the ring_buffer should contain a prefix of expected followed by
  // all zeros.
  void CheckRingBufferPartial(AudioBufferSlice ring_buffer, AudioBufferSlice expected);

  // Display portions of the snapshot buffer, for debugging purposes.
  void DisplaySnapshotSection(const AudioBuffer& ring_buffer, size_t section);

  // Submit timestamped packets. Caller must have written audio data to payload_buffer_ before
  // calling this method.
  // TODO(49981): Don't send an extra packet, once 49980 is fixed
  void SendPackets(uint32_t num_packets, int64_t initial_pts = 0, bool final_silent_packet = true);

  // Wait for packet with the given ID to complete.
  void WaitForPacket(uint32_t packet_num);

  // Call Play(), perfectly aligned with the VAD ring buffer rollover.
  void SynchronizedPlay();

  // Minimum lead time for the AudioRenderer.
  int64_t GetMinLeadTime() const { return min_lead_time_; }

  // Buffer for passing audio data to the audio_renderer_.
  fzl::VmoMapper payload_buffer_;
  fuchsia::media::AudioRendererPtr audio_renderer_;

  // Controls debugging output in CheckRingBuffer.
  const char* test_phase_;

 private:
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

  void CheckRingBufferInternal(AudioBufferSlice ring_buffer, AudioBufferSlice expected,
                               bool partial);

  void MapAndAddRendererBuffer(uint32_t buffer_id);

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
  uint32_t ring_pos_ = 0;
  uint64_t running_ring_pos_ = 0;
  zx_time_t latest_pos_notify_time_ = 0;

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
  bool received_min_lead_time_ = false;
  int64_t min_lead_time_ = -1;

  bool received_play_ = false;
  int64_t received_play_ref_time_ = 0;
  int64_t received_play_media_time_ = -1;

  bool received_packet_completion_ = false;
  int32_t received_packet_num_ = -1;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_PIPELINE_HERMETIC_AUDIO_PIPELINE_TEST_H_
