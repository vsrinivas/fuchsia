// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/pipeline/audio_pipeline_test.h"

#include <cstdint>

#include "lib/media/cpp/timeline_function.h"
#include "lib/media/cpp/timeline_rate.h"
#include "src/lib/fxl/logging.h"

namespace media::audio::test {

constexpr char kOutputUniqueId[] = "f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0";
constexpr bool kDisplaySnapshotBuffer = false;

//
// AudioPipelineTest static variables
//
fuchsia::virtualaudio::ControlSyncPtr AudioPipelineTest::control_sync_;

//
// AudioPipelineTest implementation
//

// static
// TestEnvironment stashes the ControlSync for use later
void AudioPipelineTest::SetControl(fuchsia::virtualaudio::ControlSyncPtr control_sync) {
  AudioPipelineTest::control_sync_ = std::move(control_sync);
}

// static
// Disable then reenable virtual audio, as baseline before running a test case
void AudioPipelineTest::ResetVirtualDevices() {
  DisableVirtualDevices();

  ASSERT_EQ(control_sync_->Enable(), ZX_OK);
}

// static
// Disable virtual audio, as baseline before running a test case
void AudioPipelineTest::DisableVirtualDevices() { ASSERT_EQ(control_sync_->Disable(), ZX_OK); }

// static
// This is run once after each complete repetition of the test binary
void AudioPipelineTest::TearDownTestSuite() { DisableVirtualDevices(); }

// Before each test case, set up the needed ingredients
void AudioPipelineTest::SetUp() {
  AudioCoreTestBase::SetUp();

  startup_context_->svc()->Connect(audio_dev_enum_.NewRequest());
  audio_dev_enum_.set_error_handler(ErrorHandler());

  AddVirtualOutput();
  SetUpRenderer();

  SetUpBuffers();
}

// After each test case, do final checks and clean everything up
void AudioPipelineTest::TearDown() {
  // Mute events, to avoid flakes from "unbind triggers an event elsewhere".
  ResetAudioRendererEvents();
  ResetVirtualAudioEvents();

  EXPECT_TRUE(output_.is_bound());
  output_->Remove();
  output_.Unbind();

  EXPECT_TRUE(audio_renderer_.is_bound());
  audio_renderer_.Unbind();

  // wait for audio device to remove, and for default to change
  if ((received_default_device_token_ != 0) || !received_remove_device_) {
    ExpectCondition(
        [this]() { return received_remove_device_ && (received_default_device_token_ == 0); });
  }

  ResetAudioDeviceEvents();
  EXPECT_TRUE(audio_dev_enum_.is_bound());
  audio_dev_enum_.Unbind();

  AudioCoreTestBase::TearDown();
}

// Create a virtual audio output, with the needed characteristics
void AudioPipelineTest::AddVirtualOutput() {
  startup_context_->svc()->Connect(output_.NewRequest());
  output_.set_error_handler(ErrorHandler());
  SetVirtualAudioEvents();

  std::array<uint8_t, 16> output_unique_id;
  for (auto i = 0; i < 16; ++i) {
    output_unique_id[i] = 0xF0;
  }
  output_->SetUniqueId(output_unique_id);
  output_->SetRingBufferRestrictions(kRingFrames, kRingFrames, kRingFrames);
  output_->SetNotificationFrequency(kNumRingSections);

  SetAudioDeviceEvents();
  output_->Add();

  // expect OnSetFormat (we map ring buffer in this callback)
  // Wait for device to add -- expect OnStart and OnDeviceAdded
  ExpectCondition(
      [this]() { return received_set_format_ && received_start_ && received_add_device_; });

  // Ensure device gain is unity
  if ((received_gain_db_ != 0.0f) || received_mute_) {
    fuchsia::media::AudioGainInfo unity = {.gain_db = 0.0f, .flags = 0};
    uint32_t set_flags =
        fuchsia::media::SetAudioGainFlag_GainValid | fuchsia::media::SetAudioGainFlag_MuteValid;
    audio_dev_enum_->SetDeviceGain(received_add_device_token_, unity, set_flags);

    // expect OnDeviceGainChanged
    ExpectCondition([this]() { return received_gain_changed_; });
  }

  // Wait for device to become default -- expect OnDefaultDeviceChanged
  ExpectCondition([this]() { return received_default_device_changed_; });
  ASSERT_FALSE(error_occurred_);
}

// Enable the virtual audio callbacks and ensure that responses are correct
void AudioPipelineTest::SetVirtualAudioEvents() {
  output_.events().OnSetFormat = CompletionCallback(
      [this](uint32_t fps, uint32_t fmt, uint32_t num_chans, zx_duration_t ext_delay) {
        received_set_format_ = true;
        EXPECT_EQ(fps, kDefaultFrameRate);
        EXPECT_EQ(fmt, kDefaultSampleFormat);
        EXPECT_EQ(num_chans, kDefaultNumChannels);
        EXPECT_EQ(ext_delay, kDefaultExternalDelayNs);
      });
  output_.events().OnSetGain =
      CompletionCallback([this](bool cur_mute, bool cur_agc, float cur_gain_db) {
        received_set_gain_ = true;
        gain_db_ = cur_gain_db;
        EXPECT_FALSE(cur_mute);
        EXPECT_FALSE(cur_agc);
      });
  output_.events().OnBufferCreated =
      CompletionCallback([this](zx::vmo ring_buffer_vmo, uint32_t num_ring_buffer_frames,
                                uint32_t notifications_per_ring) {
        received_ring_buffer_ = true;
        rb_vmo_ = std::move(ring_buffer_vmo);
        num_rb_frames_ = num_ring_buffer_frames;
      });
  output_.events().OnStart = CompletionCallback([this](zx_time_t start_time) {
    received_start_ = true;
    start_time_ = start_time;
  });
  output_.events().OnStop = CompletionCallback([this](zx_time_t stop_time, uint32_t ring_pos) {
    received_stop_ = true;
    stop_time_ = stop_time;
    stop_pos_ = ring_pos;
  });
  output_.events().OnPositionNotify =
      CompletionCallback([this](uint32_t ring_pos, zx_time_t clock_time) {
        // compare to prev ring_pos - if less, then add RingBufferSize()
        if (ring_pos < ring_pos_) {
          running_ring_pos_ += RingBufferSize();
        }
        running_ring_pos_ += ring_pos;
        running_ring_pos_ -= ring_pos_;
        ring_pos_ = ring_pos;
        latest_pos_notify_time_ = clock_time;
      });
}

// Disable the virtual audio callbacks
void AudioPipelineTest::ResetVirtualAudioEvents() {
  output_.events().OnSetFormat = nullptr;
  output_.events().OnSetGain = nullptr;
  output_.events().OnBufferCreated = nullptr;
  output_.events().OnStart = nullptr;
  output_.events().OnStop = nullptr;
  output_.events().OnPositionNotify = nullptr;
}

// Enable audio device enumerator callbacks; ensure that responses are correct
void AudioPipelineTest::SetAudioDeviceEvents() {
  audio_dev_enum_.events().OnDeviceAdded =
      CompletionCallback([this](fuchsia::media::AudioDeviceInfo device) {
        if (strncmp(device.unique_id.data(), kOutputUniqueId, 32) == 0) {
          received_add_device_ = true;
          received_add_device_token_ = device.token_id;
          received_gain_db_ = device.gain_info.gain_db;
          received_mute_ = device.gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute;
        } else {
          FXL_LOG(ERROR) << "Unexpected device arrival of " << device.token_id << ", unique_id '"
                         << device.unique_id << "'";
        }
      });
  audio_dev_enum_.events().OnDeviceRemoved = CompletionCallback([this](uint64_t device_token) {
    if (device_token == received_add_device_token_) {
      received_remove_device_ = true;
    } else {
      FXL_LOG(ERROR) << "Unrelated device removal of " << device_token << " (ours is "
                     << received_add_device_token_ << ")";
    }
  });
  audio_dev_enum_.events().OnDeviceGainChanged =
      CompletionCallback([this](uint64_t device_token, fuchsia::media::AudioGainInfo gain_info) {
        if (device_token == received_add_device_token_) {
          received_gain_changed_ = true;
        } else {
          FXL_LOG(ERROR) << "Unrelated device gain change of " << device_token << " (ours is "
                         << received_add_device_token_ << ")";
        }
      });
  audio_dev_enum_.events().OnDefaultDeviceChanged =
      CompletionCallback([this](uint64_t old_default_token, uint64_t new_default_token) {
        if (new_default_token == received_add_device_token_) {
          received_default_device_changed_ = true;
          received_default_device_token_ = new_default_token;
        } else if (old_default_token == received_add_device_token_) {
          EXPECT_EQ(old_default_token, received_default_device_token_);
          received_default_device_token_ = 0;
        } else {
          FXL_LOG(ERROR) << "Unrelated device default change from " << old_default_token << " to "
                         << new_default_token << " (ours is " << received_add_device_token_ << ")";
        }
      });
}

// Disable audio device enumerator callbacks
void AudioPipelineTest::ResetAudioDeviceEvents() {
  audio_dev_enum_.events().OnDeviceAdded = nullptr;
  audio_dev_enum_.events().OnDeviceGainChanged = nullptr;
  audio_dev_enum_.events().OnDeviceRemoved = nullptr;
  audio_dev_enum_.events().OnDefaultDeviceChanged = nullptr;
}

// Create an audio renderer with the needed characteristics
void AudioPipelineTest::SetUpRenderer() {
  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(ErrorHandler());
  SetAudioRendererEvents();

  audio_renderer_->SetPcmStreamType({.sample_format = kDefaultAudioFormat,
                                     .channels = kDefaultNumChannels,
                                     .frames_per_second = kDefaultFrameRate});

  audio_renderer_->SetPtsUnits(kDefaultFrameRate, 1);
}

// Enable audio renderer callbacks; store results from responses
void AudioPipelineTest::SetAudioRendererEvents() {
  audio_renderer_->EnableMinLeadTimeEvents(true);

  audio_renderer_.events().OnMinLeadTimeChanged =
      CompletionCallback([this](int64_t min_lead_time_nsec) {
        received_min_lead_time_ = true;
        min_lead_time_ = min_lead_time_nsec;
      });
}

// Disable audio renderer callbacks
void AudioPipelineTest::ResetAudioRendererEvents() {
  audio_renderer_->EnableMinLeadTimeEvents(false);
  audio_renderer_.events().OnMinLeadTimeChanged = nullptr;
}

// Retrieve the ring buffer from the virtual audio output); create our shared
// buffer with the audio renderer and map it; create a snapshot buffer for
// copying the contents of the driver ring buffer.
void AudioPipelineTest::SetUpBuffers() {
  ExpectCondition([this]() { return received_ring_buffer_; });

  // Get the ring buffer - check VMO size and map it into our address space.
  uint64_t vmo_size;
  zx_status_t status = rb_vmo_.get_size(&vmo_size);
  ASSERT_EQ(status, ZX_OK) << "Ring buffer VMO get_size failed: " << status;

  uint64_t size = static_cast<uint64_t>(kDefaultFrameSize) * num_rb_frames_;
  ASSERT_GE(vmo_size, size) << "Driver-reported ring buffer size " << size
                            << " is greater than VMO size " << vmo_size;

  zx_vm_option_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  status = ring_buffer_.Map(rb_vmo_, 0u, size, flags);
  ASSERT_EQ(status, ZX_OK) << "Ring buffer VMO map failed: " << status;
  memset(RingBufferStart(), 0, kRingBytes);

  // Create our renderer shared payload buffer, map it, send it down
  MapAndAddRendererBuffer(0);

  // Set up our snapshot buffer for copy and comparison
  compare_buff_ = std::make_unique<uint8_t[]>(kRingBytes);
  memset(compare_buff_.get(), 0, kRingBytes);
}

// Copy the ring buffer contents into our snapshot buffer. We must do this
// because the ring buffer is constantly updated by the device/driver.
void AudioPipelineTest::SnapshotRingBuffer() {
  for (auto section_num = 0u; section_num < kNumRingSections; ++section_num) {
    auto compare_section = compare_buff_.get() + (section_num * kSectionBytes);
    auto ring_buffer_section = RingBufferStart() + (section_num * kSectionBytes);
    memmove(compare_section, ring_buffer_section, kSectionBytes);

    if constexpr (kDisplaySnapshotBuffer) {
      printf("\n\n Section %d: ", section_num);
      int16_t* data_buff = reinterpret_cast<int16_t*>(compare_section);
      for (auto frame_num = 0u; frame_num < kSectionFrames; ++frame_num) {
        if (frame_num % 16 == 0) {
          printf("\n [%4d] ", frame_num);
        } else {
          printf(" | ");
        }
        for (auto chan = 0u; chan < kDefaultNumChannels; ++chan) {
          printf("%04x", 0x0ffff & data_buff[frame_num * kDefaultNumChannels + chan]);
        }
      }
      printf("\n");
    }
  }
}

// Find the first sample in the snapshot buffer that contains silence.
// We expect these to be frame-aligned, but it isn't a requirement.
uint32_t AudioPipelineTest::FirstSnapshotFrameSilence() {
  int16_t* snapshot_buffer = reinterpret_cast<int16_t*>(compare_buff_.get());
  uint32_t sample_num;
  for (sample_num = 0u; sample_num < kRingFrames * kDefaultNumChannels; ++sample_num) {
    if (snapshot_buffer[sample_num] == 0) {
      break;
    }
  }
  return sample_num / kDefaultNumChannels;
}

// Starting with the given frame, is the rest of the snapshot buffer silence?
bool AudioPipelineTest::RemainingSnapshotIsSilence(uint32_t frame_num) {
  int16_t* snapshot_buffer = reinterpret_cast<int16_t*>(compare_buff_.get());
  uint32_t start_sample = frame_num * kDefaultNumChannels;

  for (auto sample_num = start_sample; sample_num < kRingFrames * kDefaultNumChannels;
       ++sample_num) {
    if (snapshot_buffer[sample_num] != 0) {
      return false;
    }
  }
  return true;
}

// Use VmoMapper to create a VMO and map it. Pass this to the renderer.
void AudioPipelineTest::MapAndAddRendererBuffer(uint32_t buffer_id) {
  // SetUp payload buffer (400ms) and add it
  payload_buffer_.Unmap();
  zx::vmo payload_buffer_vmo;
  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  zx_status_t status =
      payload_buffer_.CreateAndMap(kRendererBytes, option_flags, nullptr, &payload_buffer_vmo,
                                   ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  EXPECT_EQ(status, ZX_OK) << "VmoMapper:::CreateAndMap failed: " << status;

  audio_renderer_->AddPayloadBuffer(buffer_id, std::move(payload_buffer_vmo));
}

// Construct a sequence of audio packets, setting the timestamps and payload
// offsets, write their audio data to the payload buffer, and send them down.
void AudioPipelineTest::CreateAndSendPackets(uint32_t num_packets, int64_t initial_pts,
                                             int16_t initial_val) {
  FXL_CHECK(num_packets <= kNumPayloads);
  received_packet_completion_ = false;

  int16_t* audio_buffer = reinterpret_cast<int16_t*>(payload_buffer_.start());
  for (uint32_t sample = 0; sample < (num_packets * kPacketFrames * kDefaultNumChannels);
       ++sample) {
    audio_buffer[sample] = initial_val + sample;
  }

  for (auto packet_num = 0u; packet_num < num_packets; ++packet_num) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = packet_num * kPacketBytes;
    packet.payload_size = kPacketBytes;
    packet.pts = initial_pts + (packet_num * kPacketFrames);

    audio_renderer_->SendPacket(packet, [this, packet_num]() {
      received_packet_completion_ = true;
      received_packet_num_ = packet_num;
    });
  }
}

// With timeout, wait for a specified packet completion
void AudioPipelineTest::WaitForPacket(uint32_t packet_num) {
  ExpectCondition([this, packet_num]() {
    return received_packet_completion_ && (received_packet_num_ >= packet_num);
  });
  ASSERT_FALSE(error_occurred_);
}

// After waiting for an entire ring buffer to go by, compute when the start of
// the following ring buffer will be, and send a timestamped Play command that
// synchronizes PTS 0 with the start of the ring buffer.
void AudioPipelineTest::SynchronizedPlay() {
  // Allow an entire ring buffer to go by
  ExpectCondition([this]() { return (running_ring_pos_ >= kRingBytes); });

  // Calculate the ref_time for Play
  auto ns_per_byte = TimelineRate(ZX_SEC(1), kDefaultFrameRate * kDefaultFrameSize);
  int64_t running_pos_for_play = ((running_ring_pos_ / kRingBytes) + 1) * kRingBytes;
  auto running_pos_to_ref_time = TimelineFunction(start_time_, 0, ns_per_byte);
  auto ref_time_for_play = running_pos_to_ref_time.Apply(running_pos_for_play);

  // On pos notif callback, call Play(ref_time,0) to align to buffer_start
  audio_renderer_->Play(ref_time_for_play, 0, [this](int64_t reference_time, int64_t media_time) {
    received_play_ = true;
    received_play_ref_time = reference_time;
    received_play_media_time_ = media_time;
  });

  ExpectCondition([this]() { return received_play_; });
  ASSERT_FALSE(error_occurred_);
}

// Do timestamped audio packets play through the renderer to the ring buffer?
TEST_F(AudioPipelineTest, RenderWithPts) {
  uint32_t num_packets = 10;
  int64_t initial_pts = 0;
  int16_t initial_data_value = 1;

  CreateAndSendPackets(num_packets, initial_pts, initial_data_value);
  SynchronizedPlay();

  // Let all packets play through the system
  WaitForPacket(9);
  SnapshotRingBuffer();

  // TODO(mpuryear): more rigorous bit-for-bit checking
  auto first_frame_zero = FirstSnapshotFrameSilence();
  EXPECT_GT(first_frame_zero, 0u) << "Ring buffer contains silence";

  EXPECT_TRUE(RemainingSnapshotIsSilence(first_frame_zero))
      << "Unexpected data was found later in the ring buffer (should be "
         "silence)";
}

// When DiscardAllPackets is issued during Playback, PTS should reset to zero.
// If it doesn't, we will observe that the second set of packets is interpreted
// as being in the past, and thus will be dropped.
TEST_F(AudioPipelineTest, DISABLED_DiscardDuringRenderResetsPts) {
  uint32_t num_packets = 10;
  int64_t initial_pts = 0;
  int16_t initial_data_value = 1;

  CreateAndSendPackets(num_packets, initial_pts, initial_data_value);
  SynchronizedPlay();

  // Load up the renderer with lots of packets, but interrupt after one
  WaitForPacket(0);

  audio_renderer_->DiscardAllPackets(CompletionCallback());
  ExpectCallback();

  SnapshotRingBuffer();

  // TODO(mpuryear): more rigorous bit-for-bit checking
  auto pre_first_frame_zero = FirstSnapshotFrameSilence();
  EXPECT_GT(pre_first_frame_zero, 0u) << "Ring buffer contains silence";

  EXPECT_TRUE(RemainingSnapshotIsSilence(pre_first_frame_zero))
      << "Unexpected data was found later in the ring buffer (should be "
         "silence)";

  num_packets = 6;
  initial_pts = kDefaultFrameRate / 1000 * 15;
  initial_data_value = 0x4000;
  CreateAndSendPackets(num_packets, initial_pts, initial_data_value);

  WaitForPacket(5);

  // Ensure all packets came back
  audio_renderer_->DiscardAllPackets(CompletionCallback());
  ExpectCallback();

  SnapshotRingBuffer();
  auto post_first_frame_zero = FirstSnapshotFrameSilence();
  EXPECT_EQ(pre_first_frame_zero, post_first_frame_zero) << "Values are not equal";
  EXPECT_GT(post_first_frame_zero, 0u) << "Ring buffer contains silence";
  EXPECT_FALSE(RemainingSnapshotIsSilence(post_first_frame_zero))
      << "Packets after the DiscardAll were lost";
}

////// Overall, need to add tests to validate various Renderer pipeline aspects
// TODO(mpuryear): add bit-for-bit validation for these tests
// TODO(mpuryear): validate the combinations of NO_TIMESTAMP (Play ref_time,
//     Play media_time, packet PTS)
// TODO(mpuryear): validate gain and ramping
// TODO(mpuryear): validate frame-rate, and fractional position
// TODO(mpuryear): validate channelization (future)
// TODO(mpuryear): validate sample format
// TODO(mpuryear): validate timing/sequence/latency of all callbacks
// TODO(mpuryear): validate various permutations of PtsUnits. Ref clocks?
// TODO(mpuryear): handle EndOfStream?
// TODO(mpuryear): test >1 payload buffer
// TODO(mpuryear): test late packets (no timestamps), gap-then-signal at driver.
//     Should include various permutations of MinLeadTime, ContinuityThreshold
// TODO(mpuryear): test packets with timestamps already played -- expect
//     truncated-signal at driver
// TODO(mpuryear): test packets with timestamps too late -- expect Renderer
//     gap-then-truncated-signal at driver
// TODO(mpuryear): test that no data is lost when Renderer Play-Pause-Play

////// Need to add similar tests for the Capture pipeline
// TODO(mpuryear): validate signal gets bit-for-bit from driver to capturer
// TODO(mpuryear): test OnPacketProduced timing etc.
// TODO(mpuryear): test OnEndOfStream
// TODO(mpuryear): test ReleasePacket
// TODO(mpuryear): test DiscardAllPackets timing etc.
// TODO(mpuryear): test DiscardAllPacketsNoReply timing etc.

}  // namespace media::audio::test
