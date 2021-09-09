// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_BASIC_LEAKY_ALLOCATOR_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_BASIC_LEAKY_ALLOCATOR_H_

#include <lib/stdcompat/span.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace trivial_allocator {

// This is the most basic trivial allocator class on which others are built.
// It exemplifies the basic API they all share.  This is a move-only object
// (move-constructible and/or move-assignable if AllocateFunction is so) that
// contains an AllocateFunction object (see below).
//
// It has a basic allocate method that takes a size in bytes and an optional
// alignment, and returns void*.  It has a deallocate method that takes void*
// and a pointer passed to deallocate must not be used any further.  But there
// is no real expectation of reusing or releasing any memory during the life of
// the allocator object.  Instead, a separate allocator object can be used for
// each set of purposes whose lifetime management is circumscribed together.
// That is, all allocations are expected to live for at least the lifetime of
// the allocator object.
//
// See <lib/trivial-allocator/typed-allocator.h> for the wrapper class
// trivial_allocator::TypedAllocator to turn this into something fit for
// std::allocator_traits and <lib/trivial-allocator/new.h> for variants of the
// C++ new operator using a BasicLeakyAllocator or one of its derivatives.
//
// AllocateFunction is some type callable as `(size_t& size, size_t alignment)`
// to use uses some underlying allocator.  It should fail if size bytes cannot
// be allocated, but the alignment is only best-available and it may return a
// less-aligned pointer if it can't do any better.  It is expected to update
// size to increase the size of the allocation for its convenience and to
// amortize the cost of calling the AllocateFunction repeatedly.
//
// The return value from AllocateFunction is some object that has roughly the
// basic API of std::unique_ptr<std::byte>: moveable, default-constructible,
// contextually convertible to bool, and with get() and release() methods that
// return std::byte* or void*.
//
// Note that though it uses this smart-pointer type for the AllocateFunction
// interface, BasicLeakyAllocator always just leaks the underlying allocations
// used in live blocks it hands out.  That is, it calls release() on the
// returned object except in error recovery cases.  Some derivative classes can
// keep track of allocations and destroy them all when the allocator is
// destroyed.

template <typename AllocateFunction>
class BasicLeakyAllocator {
 public:
  using AllocationType =
      decltype(std::declval<AllocateFunction>()(std::declval<size_t&>(), size_t{}));

  constexpr BasicLeakyAllocator() = default;

  BasicLeakyAllocator(const BasicLeakyAllocator&) = delete;

  constexpr BasicLeakyAllocator(
      std::enable_if_t<std::is_move_constructible_v<AllocateFunction>, BasicLeakyAllocator&&>
          other) noexcept
      : allocate_(std::move(other.allocate_)) {
    std::swap(frontier_, other.frontier_);
    std::swap(space_, other.space_);
    std::swap(last_new_, other.last_new_);
  }

  template <typename... Args>
  explicit constexpr BasicLeakyAllocator(Args&&... args) : allocate_(std::forward<Args>(args)...) {}

  constexpr BasicLeakyAllocator& operator=(
      std::enable_if_t<std::is_move_constructible_v<AllocateFunction>, BasicLeakyAllocator&&>
          other) {
    allocate_ = std::move(other.allocate_);
    std::swap(frontier_, other.frontier_);
    std::swap(space_, other.space_);
    std::swap(last_new_, other.last_new_);
  }

  [[nodiscard, gnu::malloc, gnu::alloc_size(2), gnu::alloc_align(3)]] constexpr void* allocate(
      size_t size, size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
    void* ptr = std::align(alignment, size, frontier_, space_);
    if (!ptr) {
      // The pending chunk can't do it.  Get a fresh one.
      size_t chunk_size = size;
      auto new_chunk = allocate_(chunk_size, alignment);
      if (!new_chunk) {
        return nullptr;
      }
      void* new_frontier = new_chunk.get();
      ptr = std::align(alignment, size, new_frontier, chunk_size);
      if (!ptr) {
        // Ok, it failed to meet the alignment requirement.  Instead, get an
        // overly large chunk to ensure it by wasting space.
        assert(std::numeric_limits<size_t>::max() - size >= alignment);
        chunk_size = size + alignment - 1;
        new_chunk = allocate_(chunk_size, alignment);
        if (!new_chunk) {
          return nullptr;
        }
        ptr = std::align(alignment, size, new_frontier, chunk_size);
        assert(ptr);
      }

      frontier_ = new_frontier;
      space_ = chunk_size;
      static_cast<void>(new_chunk.release());
    }

    frontier_ = static_cast<void*>(static_cast<std::byte*>(ptr) + size);
    space_ -= size;

    last_new_ = ptr;
    return ptr;
  }

  constexpr void deallocate(void* ptr) {
    if (!ptr) [[unlikely]] {
      return;
    }

    // We keep one pointer of bookkeeping so we can recover the last allocation
    // made if it's freed before any other allocations.  Otherwise just leak.
    if (ptr == last_new_) {
      space_ += static_cast<std::byte*>(frontier_) - static_cast<std::byte*>(ptr);
      frontier_ = ptr;
      last_new_ = nullptr;
    }
  }

  AllocateFunction& allocate_function() { return allocate_; }
  const AllocateFunction& allocate_function() const { return allocate_; }

 private:
  [[no_unique_address]] AllocateFunction allocate_;
  void* frontier_ = nullptr;
  size_t space_ = 0;
  void* last_new_ = nullptr;
};

// Deduction guides.

template <typename T>
explicit BasicLeakyAllocator(T&& allocate) -> BasicLeakyAllocator<  //
    std::conditional_t<std::is_lvalue_reference_v<T> &&
                           !std::is_const_v<std::remove_reference_t<T>>,
                       T, std::decay_t<T>>>;

}  // namespace trivial_allocator

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_BASIC_LEAKY_ALLOCATOR_H_
