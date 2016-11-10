// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_ALLOCATOR_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_ALLOCATOR_H_

#include <stdint.h>

#include <atomic>

#include "lib/ftl/logging.h"

namespace tracing {
namespace internal {

// |Allocator| models a very simple lock- and wait-free allocator
// allocating slices of memory from a larger chunk of memory.
class Allocator {
 public:
  explicit operator bool() const { return current_ < end_; }

  // Initializes this instance to allocate at most |size| bytes from |memory|.
  inline void Initialize(void* memory, size_t size) {
    FTL_DCHECK(memory);
    end_ = reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(memory) + size);
    current_ = reinterpret_cast<uintptr_t>(memory);
  }

  // Allocate tries to allocate a slice of memory of |size|, returning
  // nullptr if it fails to do so.
  inline void* Allocate(size_t size) {
    auto block = current_.fetch_add(size);
    return block + size < end_ ? reinterpret_cast<void*>(block) : nullptr;
  }

  // Clears the instance state and marks it as invalid.
  // Subsequent calls to operator bool() will return false.
  inline void Reset() { end_ = current_ = 0; }

 private:
  uintptr_t end_ = 0;
  std::atomic<uintptr_t> current_{0};
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_ALLOCATOR_H_
