// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/test/pipeline/audio_pipeline_test.h"

#include <cstdint>
#include <set>

#include "lib/media/cpp/timeline_function.h"
#include "lib/media/cpp/timeline_rate.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::test {

constexpr char kOutputUniqueId[] = "f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0";

// Should we pretty-print the entire ring buffer, every time we snapshot it?
constexpr bool kDisplaySnapshotBuffer = false;

//
// AudioPipelineTest implementation
//
fuchsia::virtualaudio::ControlSyncPtr AudioPipelineTest::control_sync_;

void AudioPipelineTest::SetUpTestSuite() {
  HermeticAudioCoreTest::SetUpTestSuite();

#ifdef NDEBUG
  Logging::Init(FX_LOG_WARNING, {"audio_pipeline_test"});
#else
  // For verbose logging, set to -media::audio::TRACE or -media::audio::SPEW
  Logging::Init(FX_LOG_INFO, {"audio_pipeline_test"});
#endif

  environment()->ConnectToService(control_sync_.NewRequest());
  control_sync_->Enable();
}

void AudioPipelineTest::TearDownTestSuite() {
  ASSERT_TRUE(control_sync_.is_bound());
  control_sync_->Disable();
  HermeticAudioCoreTest::TearDownTestSuite();
}

// Before each test case, set up the needed ingredients
void AudioPipelineTest::SetUp() {
  HermeticAudioCoreTest::SetUp();

  environment()->ConnectToService(audio_dev_enum_.NewRequest());
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
  ResetAudioDeviceEvents();

  EXPECT_TRUE(output_.is_bound());
  output_->Remove();
  output_.Unbind();

  EXPECT_TRUE(audio_renderer_.is_bound());
  audio_renderer_.Unbind();

  WaitForVirtualDeviceDepartures();

  EXPECT_TRUE(audio_dev_enum_.is_bound());
  audio_dev_enum_.Unbind();

  HermeticAudioCoreTest::TearDown();
}

// This method assumes that AudioDeviceEvents have been reset, and waits for OnDeviceRemoved for any
// remaining virtual devices in our tokens set.
void AudioPipelineTest::WaitForVirtualDeviceDepartures() {
  AUD_VLOG(TRACE) << virtual_device_tokens_.size() << " virtual devices outstanding";

  audio_dev_enum_.events().OnDeviceRemoved =
      CompletionCallback([this](uint64_t token_id) { virtual_device_tokens_.erase(token_id); });

  RunLoopUntil([this]() { return error_occurred_ || virtual_device_tokens_.empty(); });
}

// Create a virtual audio output, with the needed characteristics
void AudioPipelineTest::AddVirtualOutput() {
  environment()->ConnectToService(output_.NewRequest());
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
  RunLoopUntil(
      [this]() { return received_set_format_ && received_start_ && received_add_device_; });

  // Ensure device gain is unity
  if ((received_gain_db_ != 0.0f) || received_mute_) {
    fuchsia::media::AudioGainInfo unity = {.gain_db = 0.0f, .flags = 0};
    uint32_t set_flags =
        fuchsia::media::SetAudioGainFlag_GainValid | fuchsia::media::SetAudioGainFlag_MuteValid;
    audio_dev_enum_->SetDeviceGain(received_add_device_token_, unity, set_flags);

    // expect OnDeviceGainChanged
    RunLoopUntil([this]() { return received_gain_changed_; });
  }

  // Wait for device to become default -- expect OnDefaultDeviceChanged
  RunLoopUntil([this]() { return received_default_device_changed_; });
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
        AUD_VLOG(TRACE) << "OnSetFormat callback: " << fps << ", " << fmt << ", " << num_chans
                        << ", " << ext_delay;
      });
  output_.events().OnSetGain =
      CompletionCallback([this](bool cur_mute, bool cur_agc, float cur_gain_db) {
        received_set_gain_ = true;
        gain_db_ = cur_gain_db;
        EXPECT_FALSE(cur_mute);
        EXPECT_FALSE(cur_agc);
        AUD_VLOG(TRACE) << "OnSetGain callback: " << cur_mute << ", " << cur_agc << ", "
                        << cur_gain_db;
      });
  output_.events().OnBufferCreated =
      CompletionCallback([this](zx::vmo ring_buffer_vmo, uint32_t num_ring_buffer_frames,
                                uint32_t notifications_per_ring) {
        received_ring_buffer_ = true;
        rb_vmo_ = std::move(ring_buffer_vmo);
        num_rb_frames_ = num_ring_buffer_frames;
        AUD_VLOG(TRACE) << "OnBufferCreated callback: " << num_ring_buffer_frames << " frames, "
                        << notifications_per_ring << " notifs/ring";
      });
  output_.events().OnStart = CompletionCallback([this](zx_time_t start_time) {
    received_start_ = true;
    start_time_ = start_time;
    AUD_VLOG(TRACE) << "OnStart callback: " << start_time;
  });
  output_.events().OnStop = CompletionCallback([this](zx_time_t stop_time, uint32_t ring_pos) {
    received_stop_ = true;
    stop_time_ = stop_time;
    stop_pos_ = ring_pos;
    AUD_VLOG(TRACE) << "OnStop callback: " << stop_time << ", " << ring_pos;
  });
  output_.events().OnPositionNotify =
      CompletionCallback([this](zx_time_t monotonic_time, uint32_t ring_pos) {
        // compare to prev ring_pos - if less, then add RingBufferSize()
        if (ring_pos < ring_pos_) {
          running_ring_pos_ += RingBufferSize();
        }
        running_ring_pos_ += ring_pos;
        running_ring_pos_ -= ring_pos_;
        ring_pos_ = ring_pos;
        latest_pos_notify_time_ = monotonic_time;
        AUD_VLOG(SPEW) << "OnPositionNotify callback: " << monotonic_time << ", " << ring_pos;
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
          FX_LOGS(ERROR) << "Unrelated device arrival of " << device.token_id << ", unique_id '"
                         << device.unique_id << "'";
        }
        virtual_device_tokens_.insert(device.token_id);
      });
  audio_dev_enum_.events().OnDeviceRemoved = CompletionCallback([this](uint64_t device_token) {
    if (device_token == received_add_device_token_) {
      received_remove_device_ = true;
    } else {
      FX_LOGS(ERROR) << "Unrelated device removal of " << device_token << " (ours is "
                     << received_add_device_token_ << ")";
    }
    virtual_device_tokens_.erase(device_token);
  });
  audio_dev_enum_.events().OnDeviceGainChanged =
      CompletionCallback([this](uint64_t device_token, fuchsia::media::AudioGainInfo gain_info) {
        if (device_token == received_add_device_token_) {
          received_gain_changed_ = true;
        } else {
          FX_LOGS(ERROR) << "Unrelated device gain change of " << device_token << " (ours is "
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
          FX_LOGS(ERROR) << "Unrelated device default change from " << old_default_token << " to "
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

  RunLoopUntil([this]() { return error_occurred_ || (min_lead_time_ > 0); });
}

// Enable audio renderer callbacks; store results from responses
void AudioPipelineTest::SetAudioRendererEvents() {
  audio_renderer_->EnableMinLeadTimeEvents(true);

  audio_renderer_.events().OnMinLeadTimeChanged =
      CompletionCallback([this](int64_t min_lead_time_nsec) {
        AUD_VLOG(TRACE) << "OnMinLeadTimeChanged: " << min_lead_time_nsec;
        received_min_lead_time_ = true;
        min_lead_time_ = min_lead_time_nsec;
      });
}

// Disable audio renderer callbacks
void AudioPipelineTest::ResetAudioRendererEvents() {
  audio_renderer_->EnableMinLeadTimeEvents(false);
  audio_renderer_.events().OnMinLeadTimeChanged = nullptr;
}

// Retrieve the ring buffer from the virtual audio output); create our shared buffer with the audio
// renderer and map it; create a snapshot buffer for copying the contents of the driver ring buffer.
void AudioPipelineTest::SetUpBuffers() {
  RunLoopUntil([this]() { return received_ring_buffer_; });

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

// Copy the ring buffer contents into our snapshot buffer. We must do this because the ring buffer
// is constantly updated by the device/driver.
void AudioPipelineTest::CreateSnapshotOfRingBuffer() {
  for (auto section_num = 0u; section_num < kNumRingSections; ++section_num) {
    auto compare_section = compare_buff_.get() + (section_num * kSectionBytes);
    auto ring_buffer_section = RingBufferStart() + (section_num * kSectionBytes);
    memmove(compare_section, ring_buffer_section, kSectionBytes);

    if constexpr (kDisplaySnapshotBuffer) {
      DisplaySnapshotSection(section_num);
    }
  }
}

void AudioPipelineTest::DisplaySnapshotSection(uint32_t section) {
  printf("\n\n Section %d: ", section);
  int16_t* data_buff = reinterpret_cast<int16_t*>(compare_buff_.get() + (section * kSectionBytes));
  for (auto frame_num = 0u; frame_num < kSectionFrames; ++frame_num) {
    if (frame_num % 16 == 0) {
      printf("\n [%3x] ", frame_num);
    } else {
      printf(" | ");
    }
    for (auto chan = 0u; chan < kDefaultNumChannels; ++chan) {
      printf("%04x", 0x0ffff & data_buff[frame_num * kDefaultNumChannels + chan]);
    }
  }
  printf("\n");
}

void AudioPipelineTest::DisplaySnapshotSectionsForFrames(uint32_t first, uint32_t second,
                                                         uint32_t third, uint32_t fourth,
                                                         uint32_t fifth) {
  std::set<uint32_t> sections;
  sections.insert(0);
  sections.insert(kNumRingSections - 1);

  sections.insert(first / kSectionFrames);
  if (first) {
    sections.insert((first - 1) / kSectionFrames);
  }

  sections.insert(second / kSectionFrames);
  if (second) {
    sections.insert((second - 1) / kSectionFrames);
  }

  sections.insert(third / kSectionFrames);
  if (third) {
    sections.insert((third - 1) / kSectionFrames);
  }

  sections.insert(fourth / kSectionFrames);
  if (fourth) {
    sections.insert((fourth - 1) / kSectionFrames);
  }

  sections.insert(fifth / kSectionFrames);
  if (fifth) {
    sections.insert((fifth - 1) / kSectionFrames);
  }

  for (auto section : sections) {
    if (section < kNumRingSections) {
      DisplaySnapshotSection(section);
    }
  }
}

// Find the first frame in the snapshot buffer that contains (non)silence.
// We expect these to be frame-aligned, but it isn't a requirement.
uint32_t AudioPipelineTest::NextContiguousSnapshotFrame(bool look_for_nonzero, uint32_t frame) {
  int16_t* snapshot_buffer = reinterpret_cast<int16_t*>(compare_buff_.get());

  while (frame < kRingFrames) {
    auto sample_num = frame * kDefaultNumChannels;
    auto channel = 0u;
    for (; channel < kDefaultNumChannels; ++channel) {
      if (look_for_nonzero == (snapshot_buffer[sample_num + channel] == 0)) {
        break;
      }
    }
    if (channel == kDefaultNumChannels) {
      break;
    }
    ++frame;
  }
  return frame;
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
// initial_pts has been defaulted to 0 if no value was provided by the caller.
void AudioPipelineTest::CreateAndSendPackets(uint32_t num_packets, int16_t initial_data_value,
                                             int64_t initial_pts) {
  FX_CHECK(num_packets <= kNumPayloads);
  received_packet_completion_ = false;

  int16_t* audio_buffer = reinterpret_cast<int16_t*>(payload_buffer_.start());
  for (uint32_t sample = 0; sample < (num_packets * kPacketFrames * kDefaultNumChannels);
       ++sample) {
    audio_buffer[sample] = initial_data_value + sample;
  }

  for (auto packet_num = 0u; packet_num < num_packets; ++packet_num) {
    fuchsia::media::StreamPacket packet;
    packet.payload_offset = packet_num * kPacketBytes;
    packet.payload_size = kPacketBytes;
    packet.pts = initial_pts + (packet_num * kPacketFrames);

    AUD_VLOG(TRACE) << " sending pkt " << packet_num;
    audio_renderer_->SendPacket(packet, [this, packet_num]() {
      AUD_VLOG(TRACE) << " return: pkt " << packet_num;
      received_packet_completion_ = true;
      received_packet_num_ = packet_num;
    });
  }
}

// With timeout, wait for a specified packet completion
void AudioPipelineTest::WaitForPacket(uint32_t packet_num) {
  RunLoopUntil([this, packet_num]() {
    return received_packet_completion_ && (received_packet_num_ >= packet_num);
  });
  ASSERT_FALSE(error_occurred_);
}

// After waiting for an entire ring buffer to go by, compute when the start of the following ring
// buffer will be, and send a timestamped Play command that synchronizes PTS 0 with the start of the
// ring buffer.
void AudioPipelineTest::SynchronizedPlay() {
  // Allow an entire ring buffer to go by
  RunLoopUntil([this]() { return (running_ring_pos_ >= kRingBytes); });

  // Calculate the ref_time for Play
  auto ns_per_byte = TimelineRate(zx::sec(1).get(), kDefaultFrameRate * kDefaultFrameSize);
  int64_t running_pos_for_play = ((running_ring_pos_ / kRingBytes) + 1) * kRingBytes;
  auto running_pos_to_ref_time = TimelineFunction(start_time_, 0, ns_per_byte);
  auto ref_time_for_play = running_pos_to_ref_time.Apply(running_pos_for_play);

  // On pos notif callback, call Play(ref_time,0) to align to buffer_start
  audio_renderer_->Play(ref_time_for_play, 0, [this](int64_t reference_time, int64_t media_time) {
    received_play_ = true;
    received_play_ref_time = reference_time;
    received_play_media_time_ = media_time;
  });

  RunLoopUntil([this]() { return received_play_; });
  ASSERT_FALSE(error_occurred_);
}

// Validate that timestamped audio packets play through the renderer to the ring
// buffer as expected.
TEST_F(AudioPipelineTest, RenderWithPts) {
  ASSERT_GT(min_lead_time_, 0);
  uint32_t num_packets = zx::duration(min_lead_time_) / zx::msec(kPacketMs);
  ++num_packets;

  CreateAndSendPackets(num_packets);
  SynchronizedPlay();

  // Let all packets play through the system
  WaitForPacket(num_packets - 1);
  CreateSnapshotOfRingBuffer();

  // There should be at least something in the ring buffer.
  auto nonzero_frame = NextContiguousSnapshotFrame(true, 0);
  if (nonzero_frame) {
    DisplaySnapshotSectionsForFrames(nonzero_frame);
    EXPECT_EQ(nonzero_frame, 0u) << "Initial data was delayed";
    ASSERT_LT(nonzero_frame, kRingFrames) << "Entire ring contains silence";
  }

  // TODO(mpuryear): more rigorous bit-for-bit checking
  auto silent_frame = NextContiguousSnapshotFrame(false, nonzero_frame);
  if (silent_frame >= kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame);
    ASSERT_LT(silent_frame, kRingFrames)
        << "Ring contains no silence after frame " << nonzero_frame;
  }

  // EXPECT_LE
  if (silent_frame - nonzero_frame != num_packets * kPacketFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame);
    ASSERT_EQ(silent_frame - nonzero_frame, num_packets * kPacketFrames)
        << "Did not receive expected amount of data: from " << silent_frame << " to "
        << nonzero_frame;
  }

  auto final_nonzero_frame = NextContiguousSnapshotFrame(true, silent_frame);
  if (final_nonzero_frame != kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame, final_nonzero_frame);
    ASSERT_EQ(final_nonzero_frame, kRingFrames)
        << "Unexpected data later in ring (" << final_nonzero_frame
        << ") -- should be silence after " << silent_frame;
  }
}

// If we issue DiscardAllPackets during Playback, PTS should reset to zero. Otherwise, we would
// observe the second set of packets interpreted as being in the past, and thus dropped.
TEST_F(AudioPipelineTest, DiscardDuringRenderResetsPts) {
  ASSERT_TRUE(received_min_lead_time_);
  uint32_t num_packets = kNumPayloads;

  CreateAndSendPackets(num_packets);
  SynchronizedPlay();

  // Load the renderer with lots of packets, but interrupt after the first one.
  WaitForPacket(0);

  auto received_discard_all_callback = false;
  audio_renderer_->DiscardAllPackets(CompletionCallback([&received_discard_all_callback]() {
    received_discard_all_callback = true;
    AUD_VLOG(TRACE) << "DiscardAllPackets #1 complete";
  }));
  RunLoopUntil([this, &received_discard_all_callback]() {
    return (error_occurred_ || received_discard_all_callback);
  });

  CreateSnapshotOfRingBuffer();

  // There should be at least something in the ring buffer, since the first packet completed.
  auto nonzero_frame = NextContiguousSnapshotFrame(true, 0);
  if (nonzero_frame) {
    DisplaySnapshotSectionsForFrames(nonzero_frame);
    EXPECT_EQ(nonzero_frame, 0u) << "Initial data was delayed";
    ASSERT_LT(nonzero_frame, kRingFrames) << "Entire ring contains silence";
  }

  // The rest of the ring buffer should be empty, as remaining packets should have been discarded.
  // TODO(mpuryear): more rigorous bit-for-bit checking of the non-zero values.
  auto silent_frame = NextContiguousSnapshotFrame(false, nonzero_frame);
  if (silent_frame >= kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame);
    ASSERT_LT(silent_frame, kRingFrames)
        << "Ring contains no silence after frame " << nonzero_frame;
  }

  auto final_nonzero_frame = NextContiguousSnapshotFrame(true, silent_frame);
  if (final_nonzero_frame < kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame, final_nonzero_frame);
    ASSERT_EQ(final_nonzero_frame, kRingFrames)
        << "Unexpected data later in ring (" << final_nonzero_frame
        << ") -- should be silence after " << silent_frame;
  }

  // We interrupted the first stream without stopping. DiscardAllPackets should reset PTS to 0. Now
  // play a new stream, starting 40 ms after the new PTS 0. Between Left|Right, initial data values
  // were odd|even; these are even|odd, for quick contrast when visually inspecting the buffer.
  int16_t initial_data_value = 0x4000;
  int64_t initial_pts = kDefaultFrameRate * 40 / 1000;
  CreateAndSendPackets(num_packets, initial_data_value, initial_pts);

  received_packet_completion_ = false;
  received_packet_num_ = 0;
  WaitForPacket(num_packets - 1);

  // Ensure all packets came back
  received_discard_all_callback = false;
  audio_renderer_->DiscardAllPackets(CompletionCallback([&received_discard_all_callback]() {
    received_discard_all_callback = true;
    AUD_VLOG(TRACE) << "DiscardAllPackets #1 complete";
  }));
  RunLoopUntil([this, &received_discard_all_callback]() {
    return (error_occurred_ || received_discard_all_callback);
  });

  CreateSnapshotOfRingBuffer();

  // Start of the data previously written (before the Discard)
  auto nonzero_frame_2 = NextContiguousSnapshotFrame(true, 0);
  if (nonzero_frame_2 > 0 || nonzero_frame != nonzero_frame_2) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, nonzero_frame_2, silent_frame);
    ASSERT_EQ(nonzero_frame_2, 0u) << "After Discard/refeed, initial data was delayed";
    ASSERT_EQ(nonzero_frame, nonzero_frame_2)
        << "Before and after Discard/refeed, starts of initial data are unequal";
  }
  ASSERT_LT(nonzero_frame_2, kRingFrames) << "After Discard/refeed, entire ring contains silence";

  // TODO(mpuryear): more rigorous bit-for-bit checking
  // End of the data previously written (before the Discard)
  auto silent_frame_2 = NextContiguousSnapshotFrame(false, nonzero_frame_2);
  if (silent_frame != silent_frame_2) {
    DisplaySnapshotSectionsForFrames(nonzero_frame_2, silent_frame, silent_frame_2);
    ASSERT_EQ(silent_frame, silent_frame_2)
        << "Before and after Discard/refeed, ends of initial data are unequal";
  }

  // There will be a gap between Discard and the new stream.
  // Start of the new data written after the Discard
  nonzero_frame_2 = NextContiguousSnapshotFrame(true, silent_frame_2);
  if (nonzero_frame_2 >= kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame_2);
    ASSERT_LT(nonzero_frame_2, kRingFrames)
        << "Ring contains no data after frame " << silent_frame_2 << " ("
        << (silent_frame_2 / kSectionFrames) << ":" << std::hex << (silent_frame_2 % kSectionFrames)
        << ")";
  }

  // End of the new data written after the Discard
  silent_frame_2 = NextContiguousSnapshotFrame(false, nonzero_frame_2);
  if (silent_frame_2 >= kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame, nonzero_frame_2);
    ASSERT_LT(silent_frame_2, kRingFrames)
        << "Ring contains no silence after frame " << nonzero_frame_2 << " ("
        << (nonzero_frame_2 / kSectionFrames) << ":" << std::hex
        << (nonzero_frame_2 % kSectionFrames) << ")";
  }
  if (silent_frame_2 - nonzero_frame_2 > num_packets * kPacketFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame, nonzero_frame_2, silent_frame_2);
    EXPECT_LE(silent_frame_2 - nonzero_frame_2, num_packets * kPacketFrames)
        << "Did not receive expected amount of additional data: was from " << nonzero_frame_2
        << " (" << (nonzero_frame_2 / kSectionFrames) << ":" << std::hex
        << (nonzero_frame_2 % kSectionFrames) << ") to " << std::dec << silent_frame_2 << " ("
        << (silent_frame_2 / kSectionFrames) << ":" << std::hex << (silent_frame_2 % kSectionFrames)
        << ")";
  }

  final_nonzero_frame = NextContiguousSnapshotFrame(true, silent_frame_2);
  if (final_nonzero_frame < kRingFrames) {
    DisplaySnapshotSectionsForFrames(nonzero_frame, silent_frame, nonzero_frame_2, silent_frame_2,
                                     final_nonzero_frame);
  }
  ASSERT_EQ(final_nonzero_frame, kRingFrames)
      << "Unexpected data later in ring (" << final_nonzero_frame << " "
      << (final_nonzero_frame / kSectionFrames) << ":" << std::hex
      << (final_nonzero_frame % kSectionFrames) << ") -- should be silence after " << std::dec
      << silent_frame_2 << " (" << (silent_frame_2 / kSectionFrames) << ":" << std::hex
      << (silent_frame_2 % kSectionFrames) << ")";
}

// /// Overall, need to add tests to validate various Renderer pipeline aspects
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
