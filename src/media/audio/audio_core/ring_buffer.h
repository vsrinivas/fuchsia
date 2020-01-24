// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>

#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"

namespace media::audio {

class RingBuffer : public Stream {
 public:
  static std::shared_ptr<RingBuffer> Create(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_ring_pos_bytes, zx::vmo vmo,
      uint32_t frame_count, bool input);

  static std::shared_ptr<RingBuffer> Allocate(
      const Format& format,
      fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_ring_pos_bytes,
      uint32_t frame_count, bool input);

  RingBuffer(const Format& format,
             fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
             fzl::VmoMapper vmo_mapper, uint32_t frame_count);

  // |media::audio::Stream|
  std::optional<Buffer> LockBuffer(zx::time now, int64_t frame, uint32_t frame_count) override;
  void UnlockBuffer(bool release_buffer) override {}
  void Trim(zx::time trim_threshold) override {}
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;

  uint64_t size() const { return vmo_mapper_.size(); }
  uint32_t frames() const { return frames_; }
  uint32_t frame_size() const { return format().bytes_per_frame(); }
  uint8_t* virt() const { return reinterpret_cast<uint8_t*>(vmo_mapper_.start()); }

 private:
  TimelineFunction ReferenceClockToRingFrame() const;

  fzl::VmoMapper vmo_mapper_;
  uint32_t frames_ = 0;
  fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_ring_pos_bytes_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_RING_BUFFER_H_
