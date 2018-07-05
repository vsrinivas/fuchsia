// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_STACK_ALLOCATOR_H_
#define LIB_ESCHER_UTIL_STACK_ALLOCATOR_H_

#include <algorithm>
#include <type_traits>

#include "lib/escher/util/align.h"

namespace escher {

// A simple, fast allocator that allocates from a fixed-size region within
// itself.  It can return either pointers to uninitialized memory, or memory
// filled with a specified value.  Resetting the allocator for reuse is
// extremely fast: the amount of used memory is simply set to zero.  No
// destructors are called when the allocator is reset/destroyed; for safety,
// only trivially-destructible types can be allocated.
template <typename T, size_t N>
class StackAllocator {
 public:
  StackAllocator() : base_(NextAlignedPtr<T>(&buffer_[0])) {}

  // Return a pointer to the specified number of T objects.  Return nullptr if
  // the requested number is zero, or if there is not enough space available.
  // The contents of the pointed-to memory are undefined.
  T* Allocate(size_t count = 1) {
    if (count == 0)
      return nullptr;

    size_t to_be_used = used_ + count;
    if (to_be_used > N)
      return nullptr;

    T* ptr = base_ + used_;
    used_ = to_be_used;
    return ptr;
  }

  // Return a pointer to the specified number of T objects.  Return nullptr if
  // the requested number is zero, or if there is not enough space available.
  // Each T item in the pointed-to memory is initialized to the specified value,
  // or a default value if none is specified.
  T* AllocateFilled(size_t count = 1, const T& fill_value = T()) {
    T* ptr = Allocate(count);
    if (ptr) {
      std::fill(ptr, ptr + count, fill_value);
    }
    return ptr;
  }

  // Reset the allocator so that its memory can be reallocated.  Any pointers
  // previously obtained from Allocate() and AllocateFilled() are now invalid,
  // and should not be used.
  void Reset() { used_ = 0; }

 private:
  static_assert(std::is_trivially_destructible<T>::value,
                "T is not trivially destructable");

  size_t used_ = 0;

  // |buffer_| is not necessarily aligned to alignof(T), so we initialize
  // |base_| to point to the first properly-aligned address within |buffer_|.
  // Adding "alignof(T)" to the size of |buffer_| guarantees that "base_[N-1]"
  // fits within it.
  T* base_;
  uint8_t buffer_[N * sizeof(T) + alignof(T)];
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_STACK_ALLOCATOR_H_
