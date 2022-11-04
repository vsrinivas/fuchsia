// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_RING_BUFFER_H_

#include <lib/zx/vmo.h>

#include <memory>

#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/utils.h"
#include "src/media/audio/audio_core/v1/versioned_timeline_function.h"

namespace media::audio {

class ReadableRingBuffer;
class WritableRingBuffer;

// Base class for streams based on ring buffers.
class BaseRingBuffer {
 public:
  // A function that computes the safe read/write frame number for the current time.
  // For ReadableRingBuffers, the safe range is [safe_read_frame-frame_count+1, safe_read_frame].
  // For WritableRingBuffers, the safe range is [safe_write_frame, safe_write_frame+frame_count-1].
  using SafeReadWriteFrameFn = std::function<int64_t()>;

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
      std::shared_ptr<Clock> audio_clock, zx::vmo vmo, int64_t frame_count,
      SafeReadWriteFrameFn safe_read_frame);

  static std::shared_ptr<WritableRingBuffer> CreateWritableHardwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
      std::shared_ptr<Clock> audio_clock, zx::vmo vmo, int64_t frame_count,
      SafeReadWriteFrameFn safe_write_frame);

  struct Endpoints {
    std::shared_ptr<ReadableRingBuffer> reader;
    std::shared_ptr<WritableRingBuffer> writer;
  };

  // Creates a ring buffer with a freshly-allocated VMO.
  static Endpoints AllocateSoftwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
      std::shared_ptr<Clock> audio_clock, int64_t frame_count,
      SafeReadWriteFrameFn safe_write_frame);

  int64_t size() const { return static_cast<int64_t>(vmo_mapper_->size()); }
  int64_t frames() const { return frame_count_; }
  uint8_t* virt() const { return reinterpret_cast<uint8_t*>(vmo_mapper_->start()); }

 protected:
  BaseRingBuffer(const Format& format,
                 fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                 std::shared_ptr<Clock> audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                 int64_t frame_count);
  virtual ~BaseRingBuffer() = default;

  BaseStream::TimelineFunctionSnapshot ReferenceClockToFixedImpl() const;

  const fbl::RefPtr<RefCountedVmoMapper> vmo_mapper_;
  const int64_t frame_count_ = 0;
  const fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame_;
  std::shared_ptr<Clock> audio_clock_;
};

class ReadableRingBuffer : public ReadableStream, public BaseRingBuffer {
 public:
  // This constructor is public so it's accessible by make_shared, but it should never
  // be called directly. Use static methods in BaseRingBuffer.
  ReadableRingBuffer(const Format& format,
                     fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                     std::shared_ptr<Clock> audio_clock,
                     fbl::RefPtr<RefCountedVmoMapper> vmo_mapper, int64_t frame_count,
                     SafeReadWriteFrameFn safe_read_frame);

  // Return a duplicate handle that reads from the same underlying ring buffer but resets
  // all stream-specific state, such as the current Trim position.
  std::shared_ptr<ReadableRingBuffer> Dup() const;

  // |media::audio::ReadableStream|
  BaseStream::TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

 private:
  ReadableRingBuffer(const ReadableRingBuffer& rb);
  friend class BaseRingBuffer;

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                                     int64_t frame_count) override;
  // Since we have no buffers to free, Trim is a no-op.
  void TrimImpl(Fixed frame) override {}

  SafeReadWriteFrameFn safe_read_frame_;
};

class WritableRingBuffer : public WritableStream, public BaseRingBuffer {
 public:
  // This constructor is public so it's accessible by make_shared, but it should never
  // be called directly. Use static methods in BaseRingBuffer.
  WritableRingBuffer(const Format& format,
                     fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                     std::shared_ptr<Clock> audio_clock,
                     fbl::RefPtr<RefCountedVmoMapper> vmo_mapper, int64_t frame_count,
                     SafeReadWriteFrameFn safe_write_frame);

  // |media::audio::WritableStream|
  BaseStream::TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }
  std::optional<WritableStream::Buffer> WriteLock(int64_t frame, int64_t frame_count) override;

 private:
  friend class BaseRingBuffer;
  SafeReadWriteFrameFn safe_write_frame_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_RING_BUFFER_H_
