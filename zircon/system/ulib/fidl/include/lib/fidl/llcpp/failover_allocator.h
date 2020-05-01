// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_FAILOVER_ALLOCATOR_H_
#define LIB_FIDL_LLCPP_FAILOVER_ALLOCATOR_H_

#include "allocator.h"

namespace fidl {

// FailoverHeapAllocator first tries to allocate a given object using the specified InnerAllocator.
// If no space is available, it heap allocates the object.
// InnerAllocator must implement Allocator.
// Here is an example of a allocator with a 2Kb buffer before it touches the heap:
//   FailoverHeapAllocator<BufferAllocator<2048>>()
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

    switch (type) {
      case AllocationType::kNonArray:
        return allocation_result{
            .data = ::operator new(obj_size, std::nothrow),
            .requires_delete = true,
        };
      case AllocationType::kArray:
        return allocation_result{
            .data = ::operator new[](obj_size* count, std::nothrow),
            .requires_delete = true,
        };
    }
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_FAILOVER_ALLOCATOR_H_
