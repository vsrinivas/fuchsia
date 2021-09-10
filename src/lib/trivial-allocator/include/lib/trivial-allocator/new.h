// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_NEW_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_NEW_H_

#include <new>

#include <fbl/alloc_checker.h>

// This file provides templated overloads of the placement-new operators such
// that `new (allocator, checker) T(...)` or `new (allocator, checker) T[n]`
// work to "heap"-allocate a T or T[n] via an `allocator` that is some kind of
// `BasicLeakyAllocator<...>` object.  It and a required `fbl::AllocChecker`
// are both passed by reference in the `new` expression.
//
// Note that this does not include `operator delete` and `operator delete[]`,
// which have no way to indicate what allocator they were associated with.
// When using trivial allocators, either std::unique_ptr and other smart
// pointer types should not be used for these "heap" pointers at all or else
// the stub-delete library should also be used (and great care taken that there
// are no uses of another allocator that should have a real `operator delete`).

// Forward declaration for <lib/trivial-allocator/basic-leaky-allocator.h>.
namespace trivial_allocator {
template <typename T>
class BasicLeakyAllocator;
}  // namespace trivial_allocator

template <typename T>
[[nodiscard]] void* operator new(size_t size, trivial_allocator::BasicLeakyAllocator<T>& allocator,
                                 fbl::AllocChecker& ac) noexcept {
  void* ptr = allocator.allocate(size);
  ac.arm(size, ptr);
  return ptr;
}

template <typename T>
[[nodiscard]] void* operator new[](size_t size,
                                   trivial_allocator::BasicLeakyAllocator<T>& allocator,
                                   fbl::AllocChecker& ac) noexcept {
  return operator new(size, allocator, ac);
}

template <typename T>
[[nodiscard]] void* operator new(size_t size, std::align_val_t alignment,
                                 trivial_allocator::BasicLeakyAllocator<T>& allocator,
                                 fbl::AllocChecker& ac) noexcept {
  void* ptr = allocator.allocate(size, static_cast<size_t>(alignment));
  ac.arm(size, ptr);
  return ptr;
}

template <typename T>
[[nodiscard]] void* operator new[](size_t size, std::align_val_t alignment,
                                   trivial_allocator::BasicLeakyAllocator<T>& allocator,
                                   fbl::AllocChecker& ac) noexcept {
  return operator new(size, alignment, allocator, ac);
}

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_NEW_H_
