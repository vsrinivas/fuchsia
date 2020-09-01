// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_

#include <lib/zx/vmo.h>

#include <memory>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"

namespace media::audio {

class ReadableRingBuffer;
class WritableRingBuffer;

// Base class for streams based on ring buffers.
class BaseRingBuffer {
 public:
  // A function that computes the safe read/write frame number for the current time.
  // For ReadableRingBuffers, the safe range is [safe_read_frame-frame_count+1, safe_read_frame].
  // For WritableRingBuffers, the safe range is [safe_write_frame, safe_write_frame+frame_count-1].
  using SafeReadWriteFrameFn = fit::function<int64_t()>;

  // Creates a ring buffer buffer backed by the given |vmo|.
  //
  // Readable buffers will function as if there is an AudioInput device populating the |vmo| with
  // audio frames conforming to |format|. Essentially the ring will consider frames |frame_count|
  // frames before |ref_time_to_frac_presentation_frame(now)| to be valid.
  //
  // Conversely, writable buffers will vend out empty buffers that are up to |frame_count| frames
  // ahead of |ref_time_to_frac_presentation_frame(now)|, with the expectation there is
  // a hardware device consuming frames at the trailing edge.
  //
  // |safe_read_frame| reports the last safe read frame at the current time.
  // |safe_write_frame| reports the first safe write frame at the current time.
  static std::shared_ptr<ReadableRingBuffer> CreateReadableHardwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
      AudioClock& audio_clock, zx::vmo vmo, uint32_t frame_count,
      SafeReadWriteFrameFn safe_read_frame);

  static std::shared_ptr<WritableRingBuffer> CreateWritableHardwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
      AudioClock& audio_clock, zx::vmo vmo, uint32_t frame_count,
      SafeReadWriteFrameFn safe_write_frame);

  struct Endpoints {
    std::shared_ptr<ReadableRingBuffer> reader;
    std::shared_ptr<WritableRingBuffer> writer;
  };

  // Creates a ring buffer with a freshly-allocated VMO.
  static Endpoints AllocateSoftwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
      AudioClock& audio_clock, uint32_t frame_count, SafeReadWriteFrameFn safe_write_frame);

  uint64_t size() const { return vmo_mapper_->size(); }
  uint32_t frames() const { return frames_; }
  uint8_t* virt() const { return reinterpret_cast<uint8_t*>(vmo_mapper_->start()); }

 protected:
  BaseRingBuffer(const Format& format,
                 fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                 AudioClock& audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                 uint32_t frame_count, bool is_hardware_buffer);
  virtual ~BaseRingBuffer() = default;

  BaseStream::TimelineFunctionSnapshot ReferenceClockToFixedImpl() const;

  const fbl::RefPtr<RefCountedVmoMapper> vmo_mapper_;
  const uint32_t frames_ = 0;
  const fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame_;
  AudioClock& audio_clock_;
  const bool is_hardware_buffer_;
};

class ReadableRingBuffer : public ReadableStream, public BaseRingBuffer {
 public:
  // This constructor is public so it's accessible by make_shared, but it should never
  // be called directly. Use static methods in BaseRingBuffer.
  ReadableRingBuffer(const Format& format,
                     fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                     AudioClock& audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                     uint32_t frame_count, SafeReadWriteFrameFn safe_read_frame,
                     bool is_hardware_buffer);

  // |media::audio::ReadableStream|
  BaseStream::TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  AudioClock& reference_clock() override { return audio_clock_; }
  std::optional<ReadableStream::Buffer> ReadLock(Fixed frame, size_t frame_count) override;
  // Since we have no buffers to free, Trim is a no-op.
  void Trim(Fixed frame) override {}

 private:
  friend class BaseRingBuffer;
  SafeReadWriteFrameFn safe_read_frame_;
};

class WritableRingBuffer : public WritableStream, public BaseRingBuffer {
 public:
  // This constructor is public so it's accessible by make_shared, but it should never
  // be called directly. Use static methods in BaseRingBuffer.
  WritableRingBuffer(const Format& format,
                     fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                     AudioClock& audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                     uint32_t frame_count, SafeReadWriteFrameFn safe_write_frame,
                     bool is_hardware_buffer);

  // |media::audio::WritableStream|
  BaseStream::TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  AudioClock& reference_clock() override { return audio_clock_; }
  std::optional<WritableStream::Buffer> WriteLock(Fixed frame, size_t frame_count) override;

 private:
  friend class BaseRingBuffer;
  SafeReadWriteFrameFn safe_write_frame_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
