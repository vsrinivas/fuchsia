// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/test/pipeline/hermetic_audio_pipeline_test.h"

#include <lib/media/cpp/timeline_function.h>
#include <lib/media/cpp/timeline_rate.h>

#include <cstdint>
#include <memory>
#include <set>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"

namespace media::audio::test {

constexpr char kOutputUniqueId[] = "f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0";

// Should we pretty-print the entire ring buffer, the last time we snapshot it?
// (We can't display it every time; that impacts performance enough to cause failures.)
constexpr bool kDisplaySnapshotBuffer = false;

//
// HermeticAudioPipelineTest implementation
//
fuchsia::virtualaudio::ControlSyncPtr HermeticAudioPipelineTest::control_sync_;

void HermeticAudioPipelineTest::SetUpTestSuite(HermeticAudioEnvironment::Options options) {
  HermeticAudioCoreTest::SetUpTestSuiteWithOptions(options);

  Logging::Init(FX_LOG_INFO, {"audio_pipeline_test"});

  environment()->ConnectToService(control_sync_.NewRequest());
  control_sync_->Enable();
}

void HermeticAudioPipelineTest::TearDownTestSuite() {
  ASSERT_TRUE(control_sync_.is_bound());
  control_sync_->Disable();
  HermeticAudioCoreTest::TearDownTestSuite();
}

// Before each test case, set up the needed ingredients
void HermeticAudioPipelineTest::SetUp() {
  HermeticAudioCoreTest::SetUp();

  environment()->ConnectToService(audio_dev_enum_.NewRequest());
  audio_dev_enum_.set_error_handler(ErrorHandler());

  AddVirtualOutput();
  SetUpRenderer();

  SetUpBuffers();
}

// After each test case, do final checks and clean everything up
void HermeticAudioPipelineTest::TearDown() {
  // Mute events, to avoid flakes from "unbind triggers an event elsewhere".
  ResetAudioRendererEvents();
  ResetVirtualAudioEvents();

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

// This method changes the AudioDeviceEvents to wait for OnDeviceRemoved for any
// remaining virtual devices, and for the default to become 0.
void HermeticAudioPipelineTest::WaitForVirtualDeviceDepartures() {
  // We're waiting for our virtual output device(s) to depart
  audio_dev_enum_.events().OnDeviceRemoved = CompletionCallback([this](uint64_t device_token) {
    ASSERT_EQ(device_token, device_token_) << "Unknown device " << device_token << " removed";
    ASSERT_FALSE(device_is_default_) << "Device was removed while it was still the default!";
    device_token_ = 0;
  });

  // but we do also set handlers for the other callbacks, to flag unexpected behavior
  audio_dev_enum_.events().OnDeviceAdded =
      CompletionCallback([](fuchsia::media::AudioDeviceInfo device) {
        FAIL() << "Unknown device added (" << device.token_id << ")";
      });
  audio_dev_enum_.events().OnDeviceGainChanged =
      CompletionCallback([](uint64_t device_token, fuchsia::media::AudioGainInfo) {
        FAIL() << "Unexpected device gain change for device " << device_token;
      });
  audio_dev_enum_.events().OnDefaultDeviceChanged =
      CompletionCallback([this](uint64_t old_default_token, uint64_t new_default_token) {
        ASSERT_EQ(old_default_token, device_token_)
            << "Unknown device default change from " << old_default_token << " to "
            << new_default_token;
        device_is_default_ = false;
      });

  RunLoopUntil([this]() { return error_occurred_ || device_token_ == 0; });

  ResetAudioDeviceEvents();
}

// Create a virtual audio output, with the needed characteristics
void HermeticAudioPipelineTest::AddVirtualOutput() {
  environment()->ConnectToService(output_.NewRequest());
  output_.set_error_handler(ErrorHandler());
  SetVirtualAudioEvents();

  std::array<uint8_t, 16> output_unique_id;
  for (auto i = 0; i < 16; ++i) {
    output_unique_id[i] = 0xF0;
  }
  output_->SetUniqueId(output_unique_id);

  output_->ClearFormatRanges();
  output_->AddFormatRange(kSampleFormat, kFrameRate, kFrameRate, kNumChannels, kNumChannels,
                          kRateFamilyFlags);

  output_->SetFifoDepth(kFifoDepthBytes);
  output_->SetExternalDelay(kExternalDelay.get());

  output_->SetRingBufferRestrictions(kRingFrames, kRingFrames, kRingFrames);
  output_->SetNotificationFrequency(kNumRingSections);

  SetAudioDeviceEvents();
  output_->Add();

  // expect OnSetFormat (we map ring buffer in this callback)
  // Wait for device to add -- expect OnStart and OnDeviceAdded
  RunLoopUntil(
      [this]() { return received_set_format_ && received_start_ && received_add_device_; });

  // Ensure device gain is unity
  if ((device_gain_db_ != 0.0f) || device_mute_) {
    fuchsia::media::AudioGainInfo unity = {.gain_db = 0.0f, .flags = 0};
    uint32_t set_flags =
        fuchsia::media::SetAudioGainFlag_GainValid | fuchsia::media::SetAudioGainFlag_MuteValid;
    audio_dev_enum_->SetDeviceGain(device_token_, unity, set_flags);

    // expect OnDeviceGainChanged
    RunLoopUntil([this]() { return received_gain_changed_; });
  }

  // Wait for device to become default -- expect OnDefaultDeviceChanged
  RunLoopUntil([this]() { return device_is_default_; });
  ASSERT_FALSE(error_occurred_);
}

// Enable the virtual audio callbacks and ensure that responses are correct
void HermeticAudioPipelineTest::SetVirtualAudioEvents() {
  output_.events().OnSetFormat = CompletionCallback(
      [this](uint32_t fps, uint32_t fmt, uint32_t num_chans, zx_duration_t ext_delay) {
        received_set_format_ = true;
        EXPECT_EQ(fps, kFrameRate);
        EXPECT_EQ(fmt, kSampleFormat);
        EXPECT_EQ(num_chans, kNumChannels);
        EXPECT_EQ(ext_delay, kExternalDelay.get());
        AUD_VLOG(TRACE) << "OnSetFormat callback: " << fps << ", " << fmt << ", " << num_chans
                        << ", " << ext_delay;
      });
  output_.events().OnSetGain =
      CompletionCallback([this](bool cur_mute, bool cur_agc, float cur_gain_db) {
        received_set_gain_ = true;
        EXPECT_EQ(cur_gain_db, 0.0f);
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
void HermeticAudioPipelineTest::ResetVirtualAudioEvents() {
  output_.events().OnSetFormat = nullptr;
  output_.events().OnSetGain = nullptr;
  output_.events().OnBufferCreated = nullptr;
  output_.events().OnStart = nullptr;
  output_.events().OnStop = nullptr;
  output_.events().OnPositionNotify = nullptr;
}

// Enable audio device enumerator callbacks; ensure that responses are correct
void HermeticAudioPipelineTest::SetAudioDeviceEvents() {
  audio_dev_enum_.events().OnDeviceAdded =
      CompletionCallback([this](fuchsia::media::AudioDeviceInfo device) {
        received_add_device_ = true;
        ASSERT_EQ(strncmp(device.unique_id.data(), kOutputUniqueId, 32), 0)
            << "Unknown " << (device.is_input ? "input" : "output") << " device arrival of "
            << device.token_id << ", unique_id '" << device.unique_id << "'";

        device_token_ = device.token_id;
        device_gain_db_ = device.gain_info.gain_db;
        device_mute_ = device.gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute;

        AUD_VLOG(TRACE) << "Our device (" << device_token_ << ") has been added";
      });
  audio_dev_enum_.events().OnDeviceRemoved = CompletionCallback([this](uint64_t device_token) {
    received_remove_device_ = true;
    ASSERT_EQ(device_token, device_token_)
        << "Unknown device removal of " << device_token << " (ours is " << device_token_ << ")";

    AUD_VLOG(TRACE) << "Our output device (" << device_token_ << ") has been removed";

    ASSERT_FALSE(device_is_default_) << "Device removed while it was still default!";
    device_token_ = 0;
  });
  audio_dev_enum_.events().OnDeviceGainChanged =
      CompletionCallback([this](uint64_t device_token, fuchsia::media::AudioGainInfo gain_info) {
        received_gain_changed_ = true;
        ASSERT_EQ(device_token, device_token_) << "Unknown device gain change of " << device_token
                                               << " (ours is " << device_token_ << ")";

        AUD_VLOG(TRACE) << "Our output device (" << device_token_
                        << ") changed gain: " << gain_info.gain_db << " dB, "
                        << ((gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute) ? "MUTE"
                                                                                       : "UNMUTE");
      });
  audio_dev_enum_.events().OnDefaultDeviceChanged =
      CompletionCallback([this](uint64_t old_default_token, uint64_t new_default_token) {
        received_default_output_changed_ = true;
        ASSERT_TRUE(device_token_ == old_default_token || device_token_ == new_default_token)
            << "Unknown device default change from " << old_default_token << " to "
            << new_default_token << " (our output is " << device_token_ << ")";

        if (new_default_token == device_token_) {
          device_is_default_ = true;
          AUD_VLOG(TRACE) << "Our output device (" << device_token_ << ") is now default";
        } else {
          device_is_default_ = false;
          AUD_VLOG(TRACE) << "Our output device (" << device_token_
                          << ") is NO LONGER default. New default: " << new_default_token;
        }
      });
}

// Disable audio device enumerator callbacks
void HermeticAudioPipelineTest::ResetAudioDeviceEvents() {
  audio_dev_enum_.events().OnDeviceAdded = nullptr;
  audio_dev_enum_.events().OnDeviceGainChanged = nullptr;
  audio_dev_enum_.events().OnDeviceRemoved = nullptr;
  audio_dev_enum_.events().OnDefaultDeviceChanged = nullptr;
}

// Create an audio renderer with the needed characteristics
void HermeticAudioPipelineTest::SetUpRenderer() {
  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(ErrorHandler());
  SetAudioRendererEvents();

  audio_renderer_->SetPcmStreamType(
      {.sample_format = kAudioFormat, .channels = kNumChannels, .frames_per_second = kFrameRate});

  audio_renderer_->SetPtsUnits(kFrameRate, 1);

  RunLoopUntil([this]() { return error_occurred_ || (min_lead_time_ > 0); });
}

// Enable audio renderer callbacks; store results from responses
void HermeticAudioPipelineTest::SetAudioRendererEvents() {
  audio_renderer_->EnableMinLeadTimeEvents(true);

  audio_renderer_.events().OnMinLeadTimeChanged =
      CompletionCallback([this](int64_t min_lead_time_nsec) {
        received_min_lead_time_ = true;
        AUD_VLOG(TRACE) << "OnMinLeadTimeChanged: " << min_lead_time_nsec;
        min_lead_time_ = min_lead_time_nsec;
      });
}

// Disable audio renderer callbacks
void HermeticAudioPipelineTest::ResetAudioRendererEvents() {
  audio_renderer_->EnableMinLeadTimeEvents(false);
  audio_renderer_.events().OnMinLeadTimeChanged = nullptr;
}

// Retrieve the ring buffer from the virtual audio output); create our shared buffer with the audio
// renderer and map it; create a snapshot buffer for copying the contents of the driver ring buffer.
void HermeticAudioPipelineTest::SetUpBuffers() {
  RunLoopUntil([this]() { return received_ring_buffer_; });

  // Get the ring buffer - check VMO size and map it into our address space.
  uint64_t vmo_size;
  zx_status_t status = rb_vmo_.get_size(&vmo_size);
  ASSERT_EQ(status, ZX_OK) << "Ring buffer VMO get_size failed: " << status;

  uint64_t size = static_cast<uint64_t>(kFrameSize) * num_rb_frames_;
  ASSERT_GE(vmo_size, size) << "Driver-reported ring buffer size " << size
                            << " is greater than VMO size " << vmo_size;

  zx_vm_option_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  status = ring_buffer_.Map(rb_vmo_, 0u, size, flags);
  ASSERT_EQ(status, ZX_OK) << "Ring buffer VMO map failed: " << status;
  memset(RingBufferStart(), 0, kRingBytes);

  // Create our renderer shared payload buffer, map it, send it down
  MapAndAddRendererBuffer(0);
}
// Construct a stream of audio data. Payload data vals increase by 1 per sample.
// By default, the first value is 1. Returns a copy of the payload.
// TODO(49981): Don't send an extra packet, once 49980 is fixed
HermeticAudioPipelineTest::AudioBuffer HermeticAudioPipelineTest::GenerateSequentialAudio(
    uint64_t num_packets, int16_t initial_data_value, bool final_silent_packet) {
  FX_CHECK(num_packets <= kNumPayloads);

  const size_t num_data_samples = num_packets * kPacketSamples;
  if (final_silent_packet) {
    ++num_packets;
  }
  FX_CHECK(num_packets <= kNumPayloads);
  const size_t num_total_samples = num_packets * kPacketSamples;

  int16_t* audio_buffer = reinterpret_cast<int16_t*>(payload_buffer_.start());
  for (size_t sample = 0; sample < num_total_samples; ++sample) {
    audio_buffer[sample] = (sample < num_data_samples) ? (initial_data_value + sample) : 0;
  }

  AudioBuffer out(num_total_samples);
  memmove(&out[0], audio_buffer, num_total_samples);
  return out;
}

// Copy the ring buffer contents into our snapshot buffer. We must do this because the ring buffer
// is constantly updated by the device/driver.
HermeticAudioPipelineTest::AudioBuffer HermeticAudioPipelineTest::CreateSnapshotOfRingBuffer() {
  AudioBuffer buf(kRingBytes);
  memmove(&buf[0], RingBufferStart(), kRingBytes);

  if constexpr (kDisplaySnapshotBuffer) {
    for (auto section_num = 0u; section_num < kNumRingSections; ++section_num) {
      DisplaySnapshotSection(buf, section_num);
    }
  }

  return buf;
}

void HermeticAudioPipelineTest::CheckRingBuffer(AudioBufferSlice ring_buffer,
                                                AudioBufferSlice expected) {
  CheckRingBufferInternal(ring_buffer, expected, false);
}

void HermeticAudioPipelineTest::CheckRingBufferPartial(AudioBufferSlice ring_buffer,
                                                       AudioBufferSlice expected) {
  CheckRingBufferInternal(ring_buffer, expected, true);
}

// Compares ring_buffer to expected, reporting any differences. If ring_buffer is larger than
// expected, the extra suffix should be all zeros. If partial is true, then the ring_buffer should
// contain a prefix of the expected buffer, where the suffix should be all zeros.
//
// For example, CheckRingBufferInternal succeeds on these inputs
//   ring_buffer = {0,1,2,3,4,0,0,0,0,0}
//   expected    = {0,1,2,3,4}
//   partial     = false
//
// And these inputs:
//   ring_buffer = {0,1,2,3,0,0,0,0,0,0}
//   expected    = {0,1,2,3,4}
//   partial     = true
//
// But not these inputs:
//   ring_buffer = {0,1,2,3,0,0,0,0,0,0}
//   expected    = {0,1,2,3,4}
//   partial     = false
void HermeticAudioPipelineTest::CheckRingBufferInternal(AudioBufferSlice ring_buffer,
                                                        AudioBufferSlice expected, bool partial) {
  FX_CHECK(ring_buffer.buf);

  // Compare sample-by-sample.
  for (size_t frame = 0; frame < ring_buffer.NumFrames(); frame++) {
    for (size_t chan = 0; chan < kNumChannels; chan++) {
      int16_t got = ring_buffer.SampleAt(frame, chan);
      int16_t want = 0;
      if (frame < expected.NumFrames()) {
        want = expected.SampleAt(frame, chan);
        if (partial && got == 0 && want != got) {
          // Expect that audio data is written one complete frame at a time.
          EXPECT_EQ(0u, chan);
          // Found the end of the prefix.
          expected = AudioBufferSlice();
          want = 0;
        }
      }
      if (want != got) {
        size_t raw_frame = ring_buffer.start + frame;
        size_t raw_section = raw_frame / kSectionFrames;
        ADD_FAILURE() << std::hex << test_phase_ << ": unexpected value at frame 0x" << raw_frame
                      << ", sample 0x" << (raw_frame % kSectionFrames) << " of section 0x"
                      << raw_section << ":\n ring_buffer[0x" << raw_frame << "] = 0x" << got
                      << "\n   expected[0x" << raw_frame << "] = 0x" << want;
        DisplaySnapshotSection(*ring_buffer.buf, raw_section);
        return;
      }
    }
  }
}

void HermeticAudioPipelineTest::DisplaySnapshotSection(const AudioBuffer& ring_buffer,
                                                       size_t section) {
  printf("\n\n Section %zu: ", section);
  for (auto frame_num = 0u; frame_num < kSectionFrames; ++frame_num) {
    if (frame_num % 16 == 0) {
      printf("\n [%3x] ", frame_num);
    } else {
      printf(" | ");
    }
    for (auto chan = 0u; chan < kNumChannels; ++chan) {
      auto offset = (frame_num + (section * kSectionFrames)) * kNumChannels + chan;
      printf("%04x", 0x0ffff & ring_buffer[offset]);
    }
  }
  printf("\n");
}

// Use VmoMapper to create a VMO and map it. Pass this to the renderer.
void HermeticAudioPipelineTest::MapAndAddRendererBuffer(uint32_t buffer_id) {
  // SetUp payload buffer and add it
  payload_buffer_.Unmap();
  zx::vmo payload_buffer_vmo;
  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  zx_status_t status =
      payload_buffer_.CreateAndMap(kRendererBytes, option_flags, nullptr, &payload_buffer_vmo,
                                   ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  EXPECT_EQ(status, ZX_OK) << "VmoMapper:::CreateAndMap failed: " << status;

  audio_renderer_->AddPayloadBuffer(buffer_id, std::move(payload_buffer_vmo));
}

// Submit timestamped packets to the audio_renderer_. Caller must have written audio data to
// payload_buffer_ before calling this method.
// TODO(49981): Don't send an extra packet, once 49980 is fixed
void HermeticAudioPipelineTest::SendPackets(uint32_t num_packets, int64_t initial_pts,
                                            bool final_silent_packet) {
  if (final_silent_packet) {
    num_packets++;
  }
  FX_CHECK(num_packets <= kNumPayloads);
  received_packet_completion_ = false;

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

// With timeout, wait for a specified packet completion.
void HermeticAudioPipelineTest::WaitForPacket(uint32_t packet_num) {
  received_packet_completion_ = false;
  received_packet_num_ = -1;
  RunLoopUntil([this, packet_num]() {
    return received_packet_completion_ &&
           (received_packet_num_ >= static_cast<int32_t>(packet_num));
  });
  ASSERT_FALSE(error_occurred_);
}

// After waiting for an entire ring buffer, compute when the start of the next ring buffer will be,
// and send a timestamped Play command that synchronizes PTS 0 with the start of the ring buffer.
void HermeticAudioPipelineTest::SynchronizedPlay() {
  // Allow an entire ring buffer to go by
  RunLoopUntil([this]() { return (running_ring_pos_ >= kRingBytes); });

  // Calculate the ref_time for Play
  auto ns_per_byte = TimelineRate(zx::sec(1).get(), kFrameRate * kFrameSize);
  int64_t running_pos_for_play = ((running_ring_pos_ / kRingBytes) + 1) * kRingBytes;
  auto running_pos_to_ref_time = TimelineFunction(start_time_, 0, ns_per_byte);
  auto ref_time_for_play = running_pos_to_ref_time.Apply(running_pos_for_play);

  // On pos notif callback, call Play(ref_time,0) to align to buffer_start
  audio_renderer_->Play(ref_time_for_play, 0, [this](int64_t reference_time, int64_t media_time) {
    received_play_ = true;
    received_play_ref_time_ = reference_time;
    received_play_media_time_ = media_time;
  });

  RunLoopUntil([this]() { return received_play_; });
  ASSERT_FALSE(error_occurred_);
}

}  // namespace media::audio::test
