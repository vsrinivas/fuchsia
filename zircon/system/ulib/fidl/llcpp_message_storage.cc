// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message_storage.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

namespace fidl {

AnyMemoryResource MakeFidlAnyMemoryResource(fidl::BufferSpan buffer_span) {
  uint8_t* data = buffer_span.data;
  uint32_t capacity = buffer_span.capacity;
  uint32_t used = 0;

  return [data, capacity, used](uint32_t num_bytes) mutable -> uint8_t* {
    uint32_t used_original = used;
    if (unlikely(add_overflow(used, num_bytes, &used))) {
      // Allocation overflowed, revert to previous state.
      used = used_original;
      return nullptr;
    }
    if (used > capacity) {
      // Allocation failed, revert to previous state.
      used = used_original;
      return nullptr;
    }
    return &data[used_original];
  };
}

namespace internal {

fitx::result<fidl::Error, fidl::BufferSpan> AnyBufferAllocator::TryAllocate(uint32_t num_bytes) {
  uint8_t* buffer = Allocate(num_bytes);
  if (buffer == nullptr) {
    return fitx::error(fidl::Result::EncodeError(ZX_ERR_BUFFER_TOO_SMALL,
                                                 fidl::internal::kCallerAllocatedBufferTooSmall));
  }
  return fitx::ok(fidl::BufferSpan{buffer, num_bytes});
}

}  // namespace internal

}  // namespace fidl
