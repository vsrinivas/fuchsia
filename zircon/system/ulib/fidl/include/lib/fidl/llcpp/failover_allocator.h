// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_FAILOVER_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_FAILOVER_ALLOCATOR_H_

#include <zircon/assert.h>

#include "allocator.h"

namespace fidl {

// FailoverHeapAllocator first tries to allocate a given object using the specified InnerAllocator.
// If no space is available, it heap allocates the object.
// InnerAllocator must implement Allocator.
// Here is an example of a allocator with a 2Kb buffer before it touches the heap:
//   FailoverHeapAllocator<UnsafeBufferAllocator<2048>>()
// (aka BufferThenHeapAllocator<2048>)
template <typename InnerAllocatorType, typename... Args>
class FailoverHeapAllocator : public Allocator {
 public:
  FailoverHeapAllocator(Args... args) : inner_allocator_(args...) {}

  static_assert(std::is_base_of<Allocator, InnerAllocatorType>::value,
                "Inner allocator must implement Allocator class");

  InnerAllocatorType& inner_allocator() { return inner_allocator_; }

 private:
  InnerAllocatorType inner_allocator_;

  allocation_result allocate(AllocationType type, size_t obj_size, size_t count,
                             destructor dtor) override {
    Allocator& allocator = inner_allocator_;
    allocation_result result = allocator.allocate(type, obj_size, count, dtor);
    if (result.data != nullptr) {
      return result;
    }
    ZX_DEBUG_ASSERT(!result.data);
    return allocation_result{
        .data = nullptr,
        // Regardless of what result from delegate allocator said, have fidl::Allocator perform heap
        // allocation compatible with later delete/delete[].  The heap allocation needs to be
        // performed by fidl::Allocator::make<T>() because make<>() has the type T, which is needed
        // by the new/new[] expression, which is needed to be compatible with delete/delete[] in the
        // general case.
        .heap_allocate = true,
    };
  }
};

template <typename T, typename... Args>
class FailoverHeapAllocator<FailoverHeapAllocator<T>, Args...> {
 private:
  // This is intentionally impossible to create, since nesting FailoverHeapAllocator within
  // FailoverHeapAllocator isn't what anyone actually wants (so far at least).
  FailoverHeapAllocator() = delete;
  FailoverHeapAllocator(const FailoverHeapAllocator& to_copy) = delete;
  FailoverHeapAllocator& operator=(const FailoverHeapAllocator& to_copy) = delete;
  FailoverHeapAllocator(FailoverHeapAllocator&& to_move) = delete;
  FailoverHeapAllocator& operator=(FailoverHeapAllocator&& to_move) = delete;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_FAILOVER_ALLOCATOR_H_
