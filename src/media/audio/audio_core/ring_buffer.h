// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_

#include <lib/zx/vmo.h>

#include <memory>

#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"

namespace media::audio {

class RingBuffer : public Stream {
 public:
  // Creates a RingBuffer stream for a buffer backed by the given |vmo|.
  //
  // If |endpoint| is |kReadable|, then this ring buffer will function as if there is an AudioInput
  // device populating the |vmo| with audio frames conforming to |format|. Essentially the ring will
  // consider frames |frame_count| frames before |reference_clock_to_fractional_frames(now)| to
  // be valid.
  //
  // Conversely, if |endpoint| is |kWritable|, then this Stream will vend out empty buffers that are
  // up to |frame_count| frames ahead of |reference_clock_to_fractional_frames(now)|. With the
  // expectation there is a hardware device consuming frames at the trailing edge.
  //
  // |offset_frames| determines how logical frame numbers are mapped to physical frame locations in
  // the ring buffer. Ex: if |offset_frames| is 5, then frame 0 will be located at frame 5 in the
  // ring buffer.
  enum class Endpoint {
    kReadable,
    kWritable,
  };
  enum class VmoMapping {
    kReadOnly,
    kReadWrite,
  };
  static std::shared_ptr<RingBuffer> CreateHardwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames, zx::vmo vmo,
      uint32_t frame_count, VmoMapping vmo_mapping, Endpoint endpoint, uint32_t offset_frames);

  // An allocating variant of |Create|.
  struct Endpoints {
    std::shared_ptr<RingBuffer> reader;
    std::shared_ptr<RingBuffer> writer;
  };
  static Endpoints AllocateSoftwareBuffer(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
      uint32_t frame_count, uint32_t frame_offset = 0);

  // |media::audio::Stream|
  void UnlockBuffer(bool release_buffer) override {}
  void Trim(zx::time trim_threshold) override {}
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;

  Endpoint endpoint() const { return endpoint_; }
  uint64_t size() const { return vmo_mapper_->size(); }
  uint32_t frames() const { return frames_; }
  uint32_t frame_size() const { return format().bytes_per_frame(); }
  uint8_t* virt() const { return reinterpret_cast<uint8_t*>(vmo_mapper_->start()); }

 protected:
  RingBuffer(const Format& format,
             fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
             fbl::RefPtr<RefCountedVmoMapper> vmo_mapper, uint32_t frame_count, Endpoint endpoint,
             uint32_t offset_frames);

  uint32_t offset_frames() const { return offset_frames_; }

 private:
  Endpoint endpoint_;
  fbl::RefPtr<RefCountedVmoMapper> vmo_mapper_;
  uint32_t frames_ = 0;
  fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame_;
  uint32_t offset_frames_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
