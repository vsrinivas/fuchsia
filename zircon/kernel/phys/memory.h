// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_MEMORY_H_
#define ZIRCON_KERNEL_PHYS_MEMORY_H_

#include <zircon/boot/image.h>
#include <zircon/compiler.h>

#include <cstddef>

#include <ktl/span.h>
#include <ktl/unique_ptr.h>

// Parse the given ZBI to initialise the memory allocator with free ranges of memory.
//
// Panics on failure.
void InitMemory(const zbi_header_t* zbi);

// Attempt to allocate `size` bytes of memory with the given alignment.
//
// Return nullptr on failure.
void* AllocateMemory(size_t size, size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) __MALLOC;

// Return the given range of memory back to the allocator.
void FreeMemory(void* ptr, size_t size);

// A deleter for ktl::unique_ptr that maintains the allocation size. See
// the function `MakeUnique` below for a convenient way to use it.
struct AllocationDeleter {
  explicit AllocationDeleter(size_t size) : size(size) {}

  void operator()(void* ptr) const {
    if (ptr != nullptr) {
      FreeMemory(ptr, size);
    }
  }

  size_t size;
};

// Short-hand for a ktl::unique_ptr with a custom deleter that releases
// back into the physboot memory pool.
template <typename T>
using UniquePtr = ktl::unique_ptr<T, AllocationDeleter>;

// Create a ktl::unique_ptr for an allocation made via AllocateMemory.
//
// We different from a plain ktl::unique_unique pointer because we have a custom
// deleter (`FreeMemory`) and also need to track the size of the allocation.
template <typename T>
UniquePtr<T> AdoptAllocation(T* ptr, size_t size) {
  return ktl::unique_ptr<T, AllocationDeleter>(ptr, AllocationDeleter{size});
}

#endif  // ZIRCON_KERNEL_PHYS_MEMORY_H_
