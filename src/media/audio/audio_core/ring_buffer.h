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
  // Creates a ring buffer buffer backed by the given |vmo|.
  //
  // Readable buffers will function as if there is an AudioInput device populating the |vmo| with
  // audio frames conforming to |format|. Essentially the ring will consider frames |frame_count|
  // frames before |reference_clock_to_fractional_frames(now)| to be valid.
  //
  // Conversely, writable buffers will vend out empty buffers that are up to |frame_count| frames
  // ahead of |reference_clock_to_fractional_frames(now)|, with the expectation there is a hardware
  // device consuming frames at the trailing edge.
  //
  // |offset_frames| determines the mapping of logical frame numbers to physical locations in the
  // ring buffer. Ex: if |offset_frames| is 5, then frame 0 is located at ring buffer frame 5.
  static std::shared_ptr<ReadableRingBuffer> CreateReadableHardwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
      AudioClock& audio_clock, zx::vmo vmo, uint32_t frame_count, uint32_t offset_frames);

  static std::shared_ptr<WritableRingBuffer> CreateWritableHardwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
      AudioClock& audio_clock, zx::vmo vmo, uint32_t frame_count, uint32_t offset_frames);

  struct Endpoints {
    std::shared_ptr<ReadableRingBuffer> reader;
    std::shared_ptr<WritableRingBuffer> writer;
  };

  // Creates a ring buffer with a freshly-allocated VMO.
  static Endpoints AllocateSoftwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
      AudioClock& audio_clock, uint32_t frame_count, uint32_t frame_offset = 0);

  uint64_t size() const { return vmo_mapper_->size(); }
  uint32_t frames() const { return frames_; }
  uint32_t offset_frames() const { return offset_frames_; }
  uint8_t* virt() const { return reinterpret_cast<uint8_t*>(vmo_mapper_->start()); }

 protected:
  BaseRingBuffer(const Format& format,
                 fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
                 AudioClock& audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                 uint32_t frame_count, uint32_t offset_frames, bool is_hardware_buffer);
  virtual ~BaseRingBuffer() = default;

  BaseStream::TimelineFunctionSnapshot ReferenceClockToFractionalFramesImpl() const;

  const fbl::RefPtr<RefCountedVmoMapper> vmo_mapper_;
  const uint32_t frames_ = 0;
  const fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame_;
  AudioClock& audio_clock_;
  const uint32_t offset_frames_;
  const bool is_hardware_buffer_;
};

class ReadableRingBuffer : public ReadableStream, public BaseRingBuffer {
 public:
  // This constructor is public so it's accessible by make_shared, but it should never
  // be called directly. Use static methods in BaseRingBuffer.
  ReadableRingBuffer(const Format& format,
                     fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
                     AudioClock& audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                     uint32_t frame_count, uint32_t offset_frames, bool is_hardware_buffer);

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLock(zx::time dest_ref_time, int64_t frame,
                                                 uint32_t frame_count) override;
  void Trim(zx::time dest_ref_time) override {}
  BaseStream::TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;

  AudioClock& reference_clock() override { return audio_clock_; }
};

class WritableRingBuffer : public WritableStream, public BaseRingBuffer {
 public:
  // This constructor is public so it's accessible by make_shared, but it should never
  // be called directly. Use static methods in BaseRingBuffer.
  WritableRingBuffer(const Format& format,
                     fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
                     AudioClock& audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
                     uint32_t frame_count, uint32_t offset_frames, bool is_hardware_buffer);

  // |media::audio::WritableStream|
  std::optional<WritableStream::Buffer> WriteLock(zx::time ref_time, int64_t frame,
                                                  uint32_t frame_count) override;
  BaseStream::TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;

  AudioClock& reference_clock() override { return audio_clock_; }
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
