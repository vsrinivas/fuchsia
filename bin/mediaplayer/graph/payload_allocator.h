// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOAD_ALLOCATOR_H_
#define GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOAD_ALLOCATOR_H_

#include <memory>

#include <stddef.h>

namespace media_player {

// Abstract base class for objects that allocate buffers for packets.
class PayloadAllocator {
 public:
  // All buffers returned by |AllocatePayloadBuffer| must be aligned on
  // |kByteAlignment|-byte boundaries.
  static constexpr size_t kByteAlignment = 32;

  // Returns the smallest multiple of |kByteAlignment| that is no smaller than
  // |size|.
  static size_t AlignUp(size_t size) {
    return size = (size + kByteAlignment - 1) & ~(kByteAlignment - 1);
  }

  // Indicates whether |buffer| is aligned to |kByteAlignment| bytes.
  static bool IsAligned(void* buffer) {
    return (reinterpret_cast<uintptr_t>(buffer) & (kByteAlignment - 1)) == 0;
  }

  // Creates a default allocator, which allocates vanilla memory from the heap.
  static std::shared_ptr<PayloadAllocator> CreateDefault();

  virtual ~PayloadAllocator(){};

  // Allocates and returns a buffer of the indicated size or returns nullptr
  // if the allocation fails. Buffers returned by this method will be aligned
  // to |kByteAlignment| bytes.
  virtual void* AllocatePayloadBuffer(size_t size) = 0;

  // Releases a buffer previously allocated via AllocatePayloadBuffer.
  virtual void ReleasePayloadBuffer(void* buffer) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOAD_ALLOCATOR_H_
