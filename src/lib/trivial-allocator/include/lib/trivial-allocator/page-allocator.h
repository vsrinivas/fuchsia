// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_PAGE_ALLOCATOR_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_PAGE_ALLOCATOR_H_

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace trivial_allocator {

// trivial_allocator::PageAllocator is an AllocateFunction compatible with
// trivial_allocator::BasicLeakyAllocator.  It uses the Memory object to do
// whole-page allocations.  Its constructor forwards arguments to the Memory
// constructor, so the PageAllocator object is copyable and/or movable if the
// Memory object is.  Some Memory object implementations are provided by
// <lib/trivial-allocator/posix.h> and <lib/trivial-allocator/zircon.h>.  The
// Memory object must define a type Memory::Capability, and these methods:
//
//  * `size_t page_size() const;`
//  * `std::pair<void*, Capability> Allocate(size_t);`
//  * `void Deallocate(Capability, void*, size_t);`
//  * `void Seal(Capability, void*, size_t);`
//
// The page_size() returned must be a power of two.  The size passed to
// Allocate will always be a multiple of that size.
//
// The Capability is some default-constructible, movable object.  It's passed
// back in the Deallocate or Seal call, or just destroyed if the memory is
// leaked without being sealed.  Either Deallocate or Seal (but not both) may
// be called with the same capability, pointer, and size from an Allocate call.
// Deallocate returns the memory.  Seal makes the memory read-only.

template <class Memory>
class PageAllocator {
 public:
  static_assert(std::is_default_constructible_v<typename Memory::Capability>);
  static_assert(std::is_move_constructible_v<typename Memory::Capability>);
  static_assert(std::is_move_assignable_v<typename Memory::Capability>);

  class Allocation {
   public:
    Allocation() = default;

    Allocation(const Allocation&) = delete;

    Allocation(Allocation&& other) noexcept
        : allocator_(std::exchange(other.allocator_, nullptr)),
          capability_(std::exchange(other.capability_, {})),
          ptr_(std::exchange(other.ptr_, nullptr)),
          size_(std::exchange(other.size_, 0)) {}

    Allocation& operator=(const Allocation&) = delete;

    Allocation& operator=(Allocation&& other) noexcept {
      reset();
      allocator_ = std::exchange(other.allocator_, nullptr);
      capability_ = std::exchange(other.capability_, {});
      ptr_ = std::exchange(other.ptr_, nullptr);
      size_ = std::exchange(other.size_, 0);
      return *this;
    }

    ~Allocation() { reset(); }

    void* get() const { return ptr_; }

    explicit operator bool() const { return ptr_; }

    size_t size_bytes() const { return size_; }

    void reset() {
      if (ptr_) {
        allocator_->memory().Deallocate(std::exchange(capability_, {}),
                                        std::exchange(ptr_, nullptr), std::exchange(size_, 0));
      }
    }

    void* release() {
      capability_ = {};
      size_ = 0;
      return std::exchange(ptr_, nullptr);
    }

    // Seal the memory and then leak it.
    void Seal() && {
      allocator_->memory().Seal(std::exchange(capability_, {}), std::exchange(ptr_, nullptr),
                                std::exchange(size_, 0));
    }

    PageAllocator& allocator() const { return *allocator_; }

   private:
    friend PageAllocator;

    PageAllocator* allocator_ = nullptr;
    [[no_unique_address]] typename Memory::Capability capability_;
    void* ptr_ = nullptr;
    size_t size_ = 0;
  };
  static_assert(std::is_default_constructible_v<Allocation>);
  static_assert(std::is_nothrow_move_constructible_v<Allocation>);
  static_assert(std::is_nothrow_move_assignable_v<Allocation>);
  static_assert(!std::is_copy_constructible_v<Allocation>);
  static_assert(!std::is_copy_assignable_v<Allocation>);

  constexpr PageAllocator() = default;
  constexpr PageAllocator(const PageAllocator&) = default;
  constexpr PageAllocator(PageAllocator&&) noexcept = default;

  constexpr PageAllocator& operator=(PageAllocator&&) noexcept = default;

  template <typename... Args>
  constexpr explicit PageAllocator(Args&&... args) : memory_(std::forward<Args>(args)...) {}

  Allocation operator()(size_t& size, size_t alignment) {
    Allocation result;
    if (size <= std::numeric_limits<size_t>::max() - memory_.page_size() + 1) [[likely]] {
      size = (size + memory_.page_size() - 1) & -memory_.page_size();
      auto [ptr, capability] = memory_.Allocate(size);
      if (ptr) {
        result.allocator_ = this;
        result.capability_ = std::move(capability);
        result.ptr_ = ptr;
        result.size_ = size;
      }
    }
    return result;
  }

  Memory& memory() { return memory_; }
  const Memory& memory() const { return memory_; }

 private:
  Memory memory_;
};

}  // namespace trivial_allocator

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_PAGE_ALLOCATOR_H_
