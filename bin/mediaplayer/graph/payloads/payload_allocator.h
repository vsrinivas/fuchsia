// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_ALLOCATOR_H_
#define GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_ALLOCATOR_H_

#include <memory>
#include "garnet/bin/mediaplayer/graph/payloads/payload_buffer.h"

namespace media_player {

// An allocator for payload buffers.
class PayloadAllocator {
 public:
  // Creates a default allocator, which allocates vanilla memory from the heap.
  static std::shared_ptr<PayloadAllocator> CreateDefault();

  PayloadAllocator() = default;

  virtual ~PayloadAllocator() = default;

  // Allocates and returns a |PayloadBuffer| with the specified size. Returns
  // nullptr if the allocation fails. Buffers returned by this method will be
  // aligned to |kByteAlignment| bytes.
  virtual fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(uint64_t size) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOADS_PAYLOAD_ALLOCATOR_H_
