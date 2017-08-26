// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "slab_allocator.h"

#include "slab_buffer.h"

namespace bluetooth {
namespace common {

constexpr size_t kMaxNumSlabs = 100;
constexpr size_t kSlabSize = 32767;

using SmallBufferTraits =
    SlabBufferTraits<kSmallBufferSize, kSlabSize / kSmallBufferSize>;
using LargeBufferTraits =
    SlabBufferTraits<kLargeBufferSize, kSlabSize / kLargeBufferSize>;

using SmallAllocator = fbl::SlabAllocator<SmallBufferTraits>;
using LargeAllocator = fbl::SlabAllocator<LargeBufferTraits>;

std::unique_ptr<common::MutableByteBuffer> NewSlabBuffer(size_t size) {
  if (size <= kSmallBufferSize) {
    auto buffer = SmallAllocator::New(size);
    if (buffer)
      return buffer;
  }

  return LargeAllocator::New(size);
}

}  // namespace common
}  // namespace bluetooth

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::bluetooth::common::LargeBufferTraits,
                                      ::bluetooth::common::kMaxNumSlabs,
                                      true);
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::bluetooth::common::SmallBufferTraits,
                                      ::bluetooth::common::kMaxNumSlabs,
                                      true);
