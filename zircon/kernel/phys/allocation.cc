// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/allocation.h"

#include <lib/arch/sysreg.h>
#include <lib/arch/x86/system.h>
#include <lib/fitx/result.h>
#include <lib/memalloc/pool.h>
#include <zircon/assert.h>

#include <fbl/no_destructor.h>
#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/string_view.h>
#include <phys/main.h>

// Global memory allocation book-keeping.
memalloc::Pool& Allocation::GetPool() {
  // Use fbl::NoDestructor to avoid generation of static destructors,
  // which fails in the phys environment.
  static fbl::NoDestructor<memalloc::Pool> allocator;
  return *allocator;
}

// This is where actual allocation happens.
// The returned object is default-constructed if it fails.
Allocation Allocation::New(fbl::AllocChecker& ac, memalloc::Type type, size_t size,
                           size_t alignment) {
  Allocation alloc;
  fitx::result<fitx::failed, uint64_t> result = GetPool().Allocate(type, size, alignment);
  ac.arm(size, result.is_ok());
  if (result.is_ok()) {
    alloc.data_ = {reinterpret_cast<ktl::byte*>(result.value()), size};
  }
  return alloc;
}

// This is where actual deallocation happens.  The destructor just calls this.
void Allocation::reset() {
  if (!data_.empty()) {
    auto result = GetPool().Free(reinterpret_cast<uint64_t>(data_.data()), data_.size());
    ZX_ASSERT(result.is_ok());
    data_ = {};
  }
}
