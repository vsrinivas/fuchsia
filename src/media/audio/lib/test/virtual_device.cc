// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/virtual_device.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/format/driver_format.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio::test {

VirtualDevice::VirtualDevice(TestFixture* fixture, HermeticAudioEnvironment* environment,
                             bool is_input, const audio_stream_unique_id_t& device_id,
                             Format format, int64_t frame_count, size_t inspect_id,
                             std::optional<DevicePlugProperties> plug_properties,
                             float expected_gain_db,
                             std::optional<DeviceClockProperties> device_clock_properties)
    : is_input_(is_input),
      format_(format),
      frame_count_(frame_count),
      inspect_id_(inspect_id),
      expected_gain_db_(expected_gain_db),
      rb_(format, frame_count) {
  // Setup the device's configuration.
  fuchsia::virtualaudio::Configuration config;

  std::copy(std::begin(device_id.data), std::end(device_id.data),
            std::begin(*config.mutable_unique_id()));

  if (plug_properties) {
    *config.mutable_plug_properties() = {
        .plug_change_time = plug_properties->plug_change_time.get(),
        .plugged = plug_properties->plugged,
        .hardwired = plug_properties->hardwired,
        .can_notify = plug_properties->can_notify,
    };
  }

  if (!AudioSampleFormatToDriverSampleFormat(format_.sample_format(), &driver_format_)) {
    FX_CHECK(false) << "Failed to convert Fmt 0x" << std::hex
                    << static_cast<uint32_t>(format_.sample_format()) << " to driver format.";
  }
  config.mutable_supported_formats()->push_back({
      .sample_format_flags = driver_format_,
      .min_frame_rate = static_cast<uint32_t>(format_.frames_per_second()),
      .max_frame_rate = static_cast<uint32_t>(format_.frames_per_second()),
      .min_channels = static_cast<uint8_t>(format_.channels()),
      .max_channels = static_cast<uint8_t>(format_.channels()),
      .rate_family_flags = ASF_RANGE_FLAG_FPS_CONTINUOUS,
  });

  config.set_fifo_depth_bytes(kFifoDepthBytes);
  config.set_external_delay(kExternalDelay.to_nsecs());

  *config.mutable_ring_buffer_constraints() = {
      .min_frames = static_cast<uint32_t>(frame_count),
      .max_frames = static_cast<uint32_t>(frame_count),
      .modulo_frames = static_cast<uint32_t>(frame_count),
  };

  auto ring_buffer_ms = static_cast<uint32_t>(
      static_cast<double>(frame_count) / static_cast<double>(format_.frames_per_second()) * 1000);
  config.set_initial_notifications_per_ring(ring_buffer_ms / kNotifyMs);

  if (device_clock_properties) {
    *config.mutable_clock_properties() = {
        .domain = device_clock_properties->domain,
        .initial_rate_adjustment_ppm = device_clock_properties->initial_rate_adjustment_ppm,
    };
  }

  if (is_input) {
    fuchsia::virtualaudio::Control_AddInput_Result result;
    auto status = environment->virtual_audio_control()->AddInput(std::move(config),
                                                                 fidl_.NewRequest(), &result);
    if (status != ZX_OK) {
      ADD_FAILURE() << "Failed to call virtualaudio.Control/AddInput, status=%d", result.err();
    } else if (result.is_err()) {
      ADD_FAILURE() << "Failed to add input device, status=%d", result.err();
    }
  } else {
    fuchsia::virtualaudio::Control_AddOutput_Result result;
    auto status = environment->virtual_audio_control()->AddOutput(std::move(config),
                                                                  fidl_.NewRequest(), &result);
    if (status != ZX_OK) {
      ADD_FAILURE() << "Failed to call virtualaudio.Control/AddOutput, status=%d", result.err();
    } else if (result.is_err()) {
      ADD_FAILURE() << "Failed to add output device, status=%d", result.err();
    }
  }

  fixture->AddErrorHandler(fidl_, "VirtualAudioDevice");
  WatchEvents();
}

VirtualDevice::~VirtualDevice() {
  ResetEvents();
  // The FIDL connection will be closed when the class is destructed.
}

void VirtualDevice::ResetEvents() {
  fidl_.events().OnSetFormat = nullptr;
  fidl_.events().OnSetGain = nullptr;
  fidl_.events().OnBufferCreated = nullptr;
  fidl_.events().OnStart = nullptr;
  fidl_.events().OnStop = nullptr;
  fidl_.events().OnPositionNotify = nullptr;
}

void VirtualDevice::WatchEvents() {
  fidl_.events().OnSetFormat = [this](int32_t fps, uint32_t fmt, int32_t num_chans,
                                      zx_duration_t ext_delay) {
    received_set_format_ = true;
    EXPECT_EQ(fps, format_.frames_per_second());
    EXPECT_EQ(fmt, driver_format_);
    EXPECT_EQ(num_chans, format_.channels());
    EXPECT_EQ(ext_delay, kExternalDelay.get());
    FX_LOGS(DEBUG) << "OnSetFormat callback: " << fps << ", " << fmt << ", " << num_chans << ", "
                   << ext_delay;
  };

  fidl_.events().OnSetGain = [this](bool cur_mute, bool cur_agc, float cur_gain_db) {
    EXPECT_EQ(cur_gain_db, expected_gain_db_);
    EXPECT_FALSE(cur_mute);
    EXPECT_FALSE(cur_agc);
    FX_LOGS(DEBUG) << "OnSetGain callback: " << cur_mute << ", " << cur_agc << ", " << cur_gain_db;
  };

  fidl_.events().OnBufferCreated = [this](zx::vmo ring_buffer_vmo,
                                          uint32_t driver_reported_frame_count,
                                          uint32_t notifications_per_ring) {
    ASSERT_EQ(frame_count_, driver_reported_frame_count);
    ASSERT_TRUE(received_set_format_);
    rb_vmo_ = std::move(ring_buffer_vmo);
    rb_.MapVmo(rb_vmo_);
    FX_LOGS(DEBUG) << "OnBufferCreated callback: " << driver_reported_frame_count << " frames, "
                   << notifications_per_ring << " notifs/ring";
  };

  fidl_.events().OnStart = [this](zx_time_t start_time) {
    ASSERT_TRUE(received_set_format_);
    ASSERT_TRUE(rb_vmo_.is_valid());
    received_start_ = true;
    start_time_ = zx::time(start_time);
    // Compute a function to translate from ring buffer position to device time.
    auto ns_per_byte = TimelineRate::Product(format_.frames_per_ns().Inverse(),
                                             TimelineRate(1, format_.bytes_per_frame()));
    running_pos_to_ref_time_ = TimelineFunction(start_time_.get(), 0, ns_per_byte);
    FX_LOGS(DEBUG) << "OnStart callback: " << start_time;
  };

  fidl_.events().OnStop = [this](zx_time_t stop_time, uint32_t ring_pos) {
    received_stop_ = true;
    stop_time_ = zx::time(stop_time);
    stop_pos_ = ring_pos;
    FX_LOGS(DEBUG) << "OnStop callback: " << stop_time << ", " << ring_pos;
  };

  fidl_.events().OnPositionNotify = [this](zx_time_t monotonic_time, uint32_t ring_pos) {
    // compare to prev ring_pos - if less, then add rb_.SizeBytes().
    if (ring_pos < ring_pos_) {
      running_ring_pos_ += rb_.SizeBytes();
    }
    running_ring_pos_ += ring_pos;
    running_ring_pos_ -= ring_pos_;
    ring_pos_ = ring_pos;
    FX_LOGS(TRACE) << "OnPositionNotify callback: " << monotonic_time << ", " << ring_pos;
  };
}

zx::time VirtualDevice::NextSynchronizedTimestamp(zx::time min_time) const {
  // Compute the next synchronized position, then iterate until we find a synchronized
  // position at min_time or later.
  int64_t running_pos_sync = ((running_ring_pos_ / rb_.SizeBytes()) + 1) * rb_.SizeBytes();
  while (true) {
    zx::time sync_time = zx::time(running_pos_to_ref_time_.Apply(running_pos_sync));
    if (sync_time >= min_time) {
      return sync_time;
    }
    running_pos_sync += rb_.SizeBytes();
  }
}

int64_t VirtualDevice::RingBufferFrameAtTimestamp(zx::time ref_time) const {
  int64_t running_pos = running_pos_to_ref_time_.ApplyInverse(ref_time.get());
  return running_pos / format_.bytes_per_frame();
}

}  // namespace media::audio::test
