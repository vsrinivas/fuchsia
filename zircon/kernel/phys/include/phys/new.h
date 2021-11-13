// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_NEW_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_NEW_H_

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/new.h>
#include <zircon/assert.h>

#include "allocation.h"

// This makes it possible to do `new (gPhysNew<memalloc::Type::kFoo>, ac) T`
// and the like.  Each allocator object lives for the lifetime of physboot.
// Any space each one may have allocated during its lifetime will either be
// completely reused after handoff or it will be preserved for a particular
// handoff purpose, depending on its memalloc::Type.

template <memalloc::Type Type>
inline trivial_allocator::BasicLeakyAllocator gPhysNew([](size_t size, size_t alignment) {
  fbl::AllocChecker ac;
  Allocation result = Allocation::New(ac, Type, size, alignment);
  if (ac.check()) {
    ZX_DEBUG_ASSERT(result);
  } else {
    ZX_DEBUG_ASSERT(!result);
  }
  return result;
});

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_NEW_H_
