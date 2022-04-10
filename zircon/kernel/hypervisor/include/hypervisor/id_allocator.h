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
// IDs of type `T` will be allocated in the range [`MinId`, `MaxId`).
//
// `T` is the type of the ID, and is an integral type.
// `MaxId` is the maximum value of an ID.
// `MinId` is the minimum value of an ID. This defaults to 1.
template <typename T, T MaxId, T MinId = 1>
class IdAllocator {
 public:
  static_assert(MaxId > MinId, "MaxId must be greater than MinId");
  IdAllocator() { Reset(); }

  // Resets the allocator, and sets a new `max_id`, where `max_id` <= `MaxId`.
  void Reset(T max_id = MaxId) {
    ZX_ASSERT(max_id > MinId);
    zx_status_t status = bitmap_.Reset(max_id);
    // We use bitmap::FixedStorage, so allocation cannot fail.
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  zx::status<T> Alloc() {
    size_t first_unset;
    if (bitmap_.Get(next_, MaxId, &first_unset)) {
      if (bitmap_.Get(MinId, next_, &first_unset)) {
        return zx::error(ZX_ERR_NO_RESOURCES);
      }
    }
    zx_status_t status = bitmap_.SetOne(first_unset);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    next_ = static_cast<T>(first_unset + 1);
    if (next_ == MaxId) {
      next_ = MinId;
    }
    return zx::ok(static_cast<T>(first_unset));
  }

  zx::status<> Free(T id) {
    if (id < MinId || !bitmap_.GetOne(id)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    zx_status_t status = bitmap_.ClearOne(id);
    return zx::make_status(status);
  }

 private:
  T next_ = MinId;
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<MaxId>> bitmap_;
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_ID_ALLOCATOR_H_
