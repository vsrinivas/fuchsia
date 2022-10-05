// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/message_storage.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

namespace fidl {

AnyMemoryResource MakeFidlAnyMemoryResource(fidl::BufferSpan buffer_span) {
  class BufferSpanMemoryResource : public MemoryResource {
   public:
    explicit BufferSpanMemoryResource(fidl::BufferSpan buffer_span)
        : data_(buffer_span.data), capacity_(buffer_span.capacity) {}

    uint8_t* Allocate(uint32_t num_bytes) final {
      uint32_t used_original = used_;
      if (unlikely(add_overflow(used_, num_bytes, &used_))) {
        // Allocation overflowed, revert to previous state.
        used_ = used_original;
        return nullptr;
      }
      if (used_ > capacity_) {
        // Allocation failed, revert to previous state.
        used_ = used_original;
        return nullptr;
      }
      return &data_[used_original];
    }

   private:
    uint8_t* data_;
    uint32_t capacity_;
    uint32_t used_ = 0;
  };

  return AnyMemoryResource(std::in_place_type_t<BufferSpanMemoryResource>{}, buffer_span);
}

namespace internal {

fit::result<fidl::Error, fidl::BufferSpan> AnyBufferAllocator::TryAllocate(uint32_t num_bytes) {
  uint8_t* buffer = Allocate(num_bytes);
  if (buffer == nullptr) {
    return fit::error(fidl::Status::EncodeError(ZX_ERR_BUFFER_TOO_SMALL,
                                                fidl::internal::kCallerAllocatedBufferTooSmall));
  }
  return fit::ok(fidl::BufferSpan{buffer, num_bytes});
}

}  // namespace internal

}  // namespace fidl
