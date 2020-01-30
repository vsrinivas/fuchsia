// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PENDING_CAPTURE_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PENDING_CAPTURE_BUFFER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fbl/slab_allocator.h>

#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

struct PendingCaptureBuffer;

namespace internal {

using PcbAllocatorTraits = ::fbl::StaticSlabAllocatorTraits<std::unique_ptr<PendingCaptureBuffer>>;

}

struct PendingCaptureBuffer
    : public fbl::SlabAllocated<internal::PcbAllocatorTraits>,
      public fbl::DoublyLinkedListable<std::unique_ptr<PendingCaptureBuffer>> {
  using AllocatorTraits = internal::PcbAllocatorTraits;
  using Allocator = ::fbl::SlabAllocator<AllocatorTraits>;

  PendingCaptureBuffer(uint32_t of, uint32_t nf, fuchsia::media::AudioCapturer::CaptureAtCallback c)
      : offset_frames(of), num_frames(nf), cbk(std::move(c)) {}

  static AtomicGenerationId sequence_generator;

  const uint32_t offset_frames;
  const uint32_t num_frames;
  const fuchsia::media::AudioCapturer::CaptureAtCallback cbk;

  int64_t capture_timestamp = fuchsia::media::NO_TIMESTAMP;
  uint32_t flags = 0;
  uint32_t filled_frames = 0;
  const uint32_t sequence_number = sequence_generator.Next();
};

}  // namespace media::audio

FWD_DECL_STATIC_SLAB_ALLOCATOR(media::audio::PendingCaptureBuffer::AllocatorTraits);

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PENDING_CAPTURE_BUFFER_H_
