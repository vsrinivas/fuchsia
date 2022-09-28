// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_BASIC_OWNING_ALLOCATOR_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_BASIC_OWNING_ALLOCATOR_H_

#include <algorithm>
#include <cassert>
#include <forward_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "basic-leaky-allocator.h"

namespace trivial_allocator {

// Forward declaration for below.
template <typename AllocateFunction>
class OwningAllocateFunction;

// This takes an AllocateFunction and returns another allocator that works much
// like the object from trivial_allocator::BasicLeakyAllocator(allocator)
// would.  The difference is that the underlying allocations are all owned by
// the allocator object so all the space used for the "leaky" allocations is
// actually reclaimed when the allocator object dies.
template <typename T>
constexpr auto BasicOwningAllocator(T&& allocator) {
  using OwningT = OwningAllocateFunction<std::decay_t<T>>;
  return BasicLeakyAllocator(OwningT(std::forward<T>(allocator)));
}

// This is an adapter that wraps any AllocateFunction into one that owns all
// the allocations it returns.
template <typename RawAllocateFunction>
class OwningAllocateFunction {
 public:
  // Our "smart pointer" type is just a dumb pointer since we own everything.
  class Allocation {
   public:
    constexpr explicit operator bool() const { return ptr_; }

    constexpr void* get() const { return ptr_; }

    constexpr void* release() { return std::exchange(ptr_, nullptr); }

    constexpr void reset() { ptr_ = nullptr; }

   private:
    friend OwningAllocateFunction;

    void* ptr_ = nullptr;
  };

  constexpr OwningAllocateFunction() = default;
  constexpr OwningAllocateFunction(const OwningAllocateFunction&) = delete;
  constexpr OwningAllocateFunction(OwningAllocateFunction&&) noexcept = default;

  template <typename... Args>
  constexpr explicit OwningAllocateFunction(Args&&... args)
      : allocate_(std::forward<Args>(args)...) {}

  constexpr OwningAllocateFunction& operator=(OwningAllocateFunction&&) noexcept = default;

  constexpr auto operator()(size_t size, size_t alignment) {
    // Bump the size up and use the tail of the underlying allocation to hold
    // the owning object.
    size_t full_size = ((size + kAlign - 1) & -kAlign) + sizeof(Owned);
    Allocation result;
    auto allocated = allocate_(full_size, std::max(kAlign, alignment));
    if (allocated) {
      void* ptr =
          static_cast<std::byte*>(allocated.get()) + ((full_size & -kAlign) - sizeof(Owned));
      size_t tail_size = sizeof(Owned);
      void* aligned = std::align(kAlign, sizeof(Owned), ptr, tail_size);
      assert(aligned);

      // Chain the new allocation on.
      owned_ = new (aligned) Owned{std::move(allocated), owned_};

      result.ptr_ = owned_->allocation.get();
    }
    return result;
  }

  ~OwningAllocateFunction() {
    while (owned_) {
      // The owning object is stored in its own allocation, so move it out.
      // When this object dies at the end of the iteration, the allocation will
      // be freed and the current owner_ pointer will become invalid.
      RawAllocation dead_allocation = std::move(owned_->allocation);

      // This destructor is surely a no-op, but it's proper semantics to
      // destroy the moved-from object.
      owned_->allocation.~RawAllocation();

      // Fetch the next allocation to free.  The owned_ pointer is still valid
      // until dead_allocation really dies at the end of the block.
      owned_ = owned_->next;
    }
  }

 private:
  using RawAllocation = std::invoke_result_t<RawAllocateFunction, size_t&, size_t>;

  struct Owned {
    RawAllocation allocation;
    Owned* next = nullptr;
  };

  static constexpr size_t kAlign = alignof(Owned);

  RawAllocateFunction allocate_;
  Owned* owned_ = nullptr;
};

}  // namespace trivial_allocator

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_BASIC_OWNING_ALLOCATOR_H_
