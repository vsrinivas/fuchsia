// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/pending_capture_buffer.h"

// Allow (at most) 256 slabs of pending capture buffers. At 16KB per slab, this
// means we will deny allocations after 4MB. If we ever need more than 4MB of
// pending capture buffer bookkeeping, something has gone seriously wrong.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(media::audio::PendingCaptureBuffer::AllocatorTraits, 0x100);

namespace media::audio {

// static
AtomicGenerationId PendingCaptureBuffer::sequence_generator;

}  // namespace media::audio
