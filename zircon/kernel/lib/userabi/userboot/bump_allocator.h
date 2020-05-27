// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BUMP_ALLOCATOR_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BUMP_ALLOCATOR_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "mapper.h"

// Trivial bump allocator with a fixed-size heap allocated in a VMO.  It leaks
// all freed memory.  This implementation is actually entirely kosher standard
// C++ (though not a kosher implementation of the standard library functions
// because this implementation is not thread-safe and malloc et al must be).
class BumpAllocator {
 public:
  // The given |vmar| must remain valid for the lifetime of the |BumpAllocator|.
  BumpAllocator(const zx::vmar* vmar);
  ~BumpAllocator();

  // Must be called before malloc, calloc, free, or realloc.
  // If this function returns an error, the BumpAllocator cannot be used.
  zx_status_t Init(size_t heap_size);

  void* malloc(size_t n);
  void free(void* ptr);

 private:
  Mapper mapper_;
  zx::vmo vmo_;

  std::byte* heap_ = nullptr;
  size_t heap_size_ = 0u;

  void* last_block_ = nullptr;
  size_t frontier_ = 0u;
};

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_BUMP_ALLOCATOR_H_
