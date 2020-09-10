// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_VIRTUAL_DEVICE_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_VIRTUAL_DEVICE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>

#include <memory>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"
#include "src/media/audio/lib/test/test_fixture.h"
#include "src/media/audio/lib/test/vmo_backed_buffer.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio::test {

struct DevicePlugProperties {
  zx::time plug_change_time;
  bool plugged;
  bool hardwired;
  bool can_notify;
};

// This class is thread hostile: none of its methods can be called concurrently.
template <class Interface>
class VirtualDevice {
 public:
  static constexpr uint32_t kNotifyMs = 10;
  static constexpr uint32_t kFifoDepthBytes = 0;
  static constexpr auto kExternalDelay = zx::msec(0);

  ~VirtualDevice();

  fidl::InterfacePtr<Interface>& fidl() { return fidl_; }
  size_t frame_count() const { return frame_count_; }

  uint64_t token() const { return token_; }
  void set_token(uint64_t t) { token_ = t; }

  // Reports whether the device has started.
  bool Ready() const { return received_start_; }

  // Returns a timestamp in the future that corresponds to byte 0 of the ring buffer.
  // The returned time is guaranteed to be at least min_time in the future, even if that
  // means waiting for more than one round trip through the ring buffer.
  zx::time NextSynchronizedTimestamp(zx::time min_time = zx::time(0)) const;

  // Returns the absolute ring buffer frame number corresponding to the given time. The
  // "absolute" frame number starts at zero and increases monotonically. The actual ring
  // buffer offset is given by absolute_frame_number % ring_buffer_size.
  int64_t RingBufferFrameAtTimestamp(zx::time ref_time) const;

  // For validating properties exported by inspect.
  size_t inspect_id() const { return inspect_id_; }

 protected:
  VirtualDevice(TestFixture* fixture, HermeticAudioEnvironment* environment,
                const audio_stream_unique_id_t& device_id, Format format, size_t frame_count,
                size_t inspect_id, std::optional<DevicePlugProperties> plug_properties,
                float expected_gain_db);

  void ResetEvents();
  void WatchEvents();

  const Format format_;
  const size_t frame_count_;
  const size_t inspect_id_;
  const float expected_gain_db_;

  fidl::InterfacePtr<Interface> fidl_;
  audio_sample_format_t driver_format_;
  zx::vmo rb_vmo_;
  VmoBackedBuffer rb_;
  bool received_set_format_ = false;
  bool received_start_ = false;
  bool received_stop_ = false;
  zx::time start_time_;
  zx::time stop_time_;
  TimelineFunction running_pos_to_ref_time_;
  uint64_t stop_pos_ = 0;
  uint64_t ring_pos_ = 0;
  uint64_t running_ring_pos_ = 0;
  uint64_t token_;
};

using VirtualOutputImpl = VirtualDevice<fuchsia::virtualaudio::Output>;
using VirtualInputImpl = VirtualDevice<fuchsia::virtualaudio::Input>;

template <fuchsia::media::AudioSampleFormat SampleFormat>
class VirtualOutput : public VirtualOutputImpl {
 public:
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  // Take a snapshot of the device's ring buffer.
  AudioBuffer<SampleFormat> SnapshotRingBuffer() { return rb_.Snapshot<SampleFormat>(); }

  // Don't call this directly. Use HermeticAudioTest::CreateOutput so the object is
  // appropriately bound into the test environment.
  VirtualOutput(TestFixture* fixture, HermeticAudioEnvironment* environment,
                const audio_stream_unique_id_t& device_id, Format format, size_t frame_count,
                size_t inspect_id, std::optional<DevicePlugProperties> plug_properties,
                float expected_gain_db)
      : VirtualDevice(fixture, environment, device_id, format, frame_count, inspect_id,
                      plug_properties, expected_gain_db) {}
};

template <fuchsia::media::AudioSampleFormat SampleFormat>
class VirtualInput : public VirtualInputImpl {
 public:
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  // Write a slice to the ring buffer at the given absolute frame number.
  void WriteRingBufferAt(size_t ring_pos_in_frames, AudioBufferSlice<SampleFormat> slice) {
    rb_.WriteAt<SampleFormat>(ring_pos_in_frames, slice);
  }

  // Don't call this directly. Use HermeticAudioTest::CreateInput so the object is
  // appropriately bound into the test environment.
  VirtualInput(TestFixture* fixture, HermeticAudioEnvironment* environment,
               const audio_stream_unique_id_t& device_id, Format format, size_t frame_count,
               size_t inspect_id, std::optional<DevicePlugProperties> plug_properties,
               float expected_gain_db)
      : VirtualDevice(fixture, environment, device_id, format, frame_count, inspect_id,
                      plug_properties, expected_gain_db) {}
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_VIRTUAL_DEVICE_H_
