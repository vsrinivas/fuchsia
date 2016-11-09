// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

namespace media {

// Abstract base class for objects that allocate buffers for packets.
class PayloadAllocator {
 public:
  // Gets the default allocator, which allocates vanilla memory from the heap.
  static PayloadAllocator* GetDefault();

  // Allocates and returns a buffer of the indicated size or returns nullptr
  // if the allocation fails.
  virtual void* AllocatePayloadBuffer(size_t size) = 0;

  // Releases a buffer previously allocated via AllocatePayloadBuffer.
  virtual void ReleasePayloadBuffer(void* buffer) = 0;
};

}  // namespace media
