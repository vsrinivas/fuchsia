// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer/test_support/array_buffer.h"

#include <zircon/assert.h>

namespace storage {

ArrayBuffer::ArrayBuffer(size_t capacity, uint32_t block_size)
    : block_size_(block_size), capacity_(capacity) {
  if (capacity > 0) {
    buffer_ = std::make_unique<char[]>(block_size * capacity);
  }
}

void* ArrayBuffer::Data(size_t index) {
  return const_cast<void*>(const_cast<const ArrayBuffer*>(this)->Data(index));
}

const void* ArrayBuffer::Data(size_t index) const {
  ZX_DEBUG_ASSERT(index < capacity_);
  return &buffer_[index * block_size_];
}

}  // namespace storage
