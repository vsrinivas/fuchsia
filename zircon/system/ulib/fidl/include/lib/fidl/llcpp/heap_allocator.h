// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_HEAP_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_HEAP_ALLOCATOR_H_

#include <zircon/assert.h>

#include "allocator.h"

namespace fidl {

// Always allocates owned tracking_ptr<> on the heap.
//
// Iff your use case involves allocations that never outlive their allocator, consider using
// BufferThenHeapAllocator<NBytes>, which has an internal buffer but also provides heap fallback.
//
// HeapAllocator creates allocations which are completely independent of the allocator, which can
// safely out-live the allocator.  Allocations by different HeapAllocator instances are not tied
// to their source HeapAllocator in any way.  All the allocations by any HeapAllocator(s) are only
// tied to the heap, not the HeapAllocator that created them.
//
// Usage:
// HeapAllocator allocator; // the allocator itself can be stored anywhere including global/static
// allocator.make<uint32_t>(12);
class HeapAllocator final : public Allocator {
 public:
  HeapAllocator() = default;
  allocation_result allocate(AllocationType type, size_t obj_size, size_t count,
                             destructor dtor) override {
    return allocation_result{
        .data = nullptr,
        // Always punt to fidl::Allocator heap failover logic.
        .heap_allocate = true,
    };
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_HEAP_ALLOCATOR_H_
