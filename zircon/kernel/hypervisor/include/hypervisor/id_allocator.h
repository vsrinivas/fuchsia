// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_ID_ALLOCATOR_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_ID_ALLOCATOR_H_

#include <lib/zx/status.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>

namespace hypervisor {

// Allocates architecture-specific resource IDs.
//
// `T` is the type of the ID, and is an integral type.
// `Max` is the maximum value of an ID.
template <typename T, T Max>
class IdAllocator {
 public:
  IdAllocator() { Reset(); }

  // Resets the allocator, and sets a new `max_size`, where `max_size` <= `Max`.
  void Reset(T max_size = Max) {
    zx_status_t status = bitmap_.Reset(max_size);
    // We use bitmap::FixedStorage, so allocation cannot fail.
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  zx::status<T> Alloc() {
    size_t first_unset;
    bool all_set = bitmap_.Get(0, bitmap_.size(), &first_unset);
    if (all_set) {
      return zx::error(ZX_ERR_NO_RESOURCES);
    }
    zx_status_t status = bitmap_.SetOne(first_unset);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(static_cast<T>(first_unset + 1));
  }

  zx::status<> Free(T id) {
    if (id == 0 || !bitmap_.GetOne(id - 1)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    zx_status_t status = bitmap_.ClearOne(id - 1);
    return zx::make_status(status);
  }

 private:
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<Max>> bitmap_;
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_ID_ALLOCATOR_H_
