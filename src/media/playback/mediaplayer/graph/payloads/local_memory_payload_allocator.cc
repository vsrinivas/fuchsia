// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/payloads/local_memory_payload_allocator.h"

#include <fbl/ref_ptr.h>

#include "src/media/playback/mediaplayer/graph/payloads/payload_buffer.h"

namespace media_player {

// static
fbl::RefPtr<LocalMemoryPayloadAllocator> LocalMemoryPayloadAllocator::Create() {
  return fbl::MakeRefCounted<LocalMemoryPayloadAllocator>();
}

void LocalMemoryPayloadAllocator::Dump(std::ostream& os) const {
  os << "no report (not tracking allocations)";
}

fbl::RefPtr<PayloadBuffer> LocalMemoryPayloadAllocator::AllocatePayloadBuffer(
    uint64_t size) {
  return PayloadBuffer::CreateWithMalloc(size);
}

}  // namespace media_player
