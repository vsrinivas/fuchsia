// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_SINGLE_HEAP_ALLOCATOR_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_SINGLE_HEAP_ALLOCATOR_H_

#include <lib/stdcompat/functional.h>
#include <lib/stdcompat/span.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>

namespace trivial_allocator {

// This returns an AllocateFunction-compatible callable object (lambda) that
// simply hands out a single span as its available buffer space.  (See comments
// in <lib/trivial-allocator/basic-leaky-allocator.h> for more API details.)
// The "smart pointer" objects it returns never actually hold any ownership.

class SingleHeapAllocator {
 public:
  using Bytes = cpp20::span<std::byte>;

  // This is the non-owning "smart pointer" type returned below.
  // It's sufficient for BasicLeakyAllocator's API requirements.
  class Allocation {
   public:
    constexpr Allocation() = default;

    constexpr Allocation(const Allocation&) = delete;

    constexpr Allocation(Allocation&& other) noexcept : ptr_(other.release()) {}

    constexpr Allocation& operator=(Allocation&& other) noexcept {
      std::swap(ptr_, other.ptr_);
      return *this;
    }

    constexpr explicit operator bool() const { return ptr_; }

    constexpr void* get() { return ptr_; }

    [[nodiscard]] constexpr void* release() { return std::exchange(ptr_, nullptr); }

   private:
    friend SingleHeapAllocator;

    void* ptr_ = nullptr;
  };

  constexpr SingleHeapAllocator() = default;

  constexpr SingleHeapAllocator(const SingleHeapAllocator&) = delete;

  constexpr SingleHeapAllocator(SingleHeapAllocator&& other) noexcept
      : heap_(std::exchange(other.heap_, {})) {}

  explicit constexpr SingleHeapAllocator(cpp20::span<std::byte> heap) : heap_(heap) {}

  Allocation operator()(size_t& size, size_t alignment) {
    Allocation result;
    if (size <= heap_.size()) {
      // We always ignore the alignment requested and just give back the whole
      // heap the first time it's enough, leaving nothing for the second time.
      // Update the caller's value to reflect exactly how much we have.
      size = heap_.size_bytes();
      result.ptr_ = heap_.data();
      heap_ = {};
    }
    return result;
  }

 private:
  cpp20::span<std::byte> heap_;
};

}  // namespace trivial_allocator

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_SINGLE_HEAP_ALLOCATOR_H_
