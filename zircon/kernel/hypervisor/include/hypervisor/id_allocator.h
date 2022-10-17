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
#include <kernel/mutex.h>
#include <ktl/type_traits.h>

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
  static_assert(ktl::is_unsigned_v<T>, "T must be unsigned");
  static_assert(MaxId > MinId, "MaxId must be greater than MinId");

  IdAllocator() {
    auto result = Reset();
    // We use `FixedStorage` and we statically assert `MaxId` > `MinId`,
    // therefore this should not fail.
    ZX_DEBUG_ASSERT(result.is_ok());
  }

  // Resets the allocator, and sets a new `max_id`, where:
  // `MinId` < `max_id` <= `MaxId`.
  zx::result<> Reset(T max_id = MaxId) {
    if (max_id <= MinId || max_id > MaxId) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    Guard<Mutex> lock{&mutex_};
    zx_status_t status = bitmap_.Reset(max_id);
    return zx::make_result(status);
  }

  zx::result<T> TryAlloc() {
    Guard<Mutex> lock{&mutex_};
    auto id = AllocFromHint(next_);
    if (id.is_ok()) {
      UpdateNext(*id);
    }
    return id;
  }

  zx::result<> Free(T id) {
    Guard<Mutex> lock{&mutex_};
    if (!bitmap_.GetOne(id)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    zx_status_t status = bitmap_.ClearOne(id);
    return zx::make_result(status);
  }

 private:
  zx::result<T> AllocFromHint(T next) TA_REQ(mutex_) {
    size_t first_unset;
    if (bitmap_.Get(next, MaxId, &first_unset)) {
      if (bitmap_.Get(MinId, next, &first_unset)) {
        return zx::error(ZX_ERR_NO_RESOURCES);
      }
    }
    zx_status_t status = bitmap_.SetOne(first_unset);
    // The bitmap returned this index as unset, therefore this should not fail.
    ZX_DEBUG_ASSERT(status == ZX_OK);
    auto val = static_cast<T>(first_unset);
    return zx::ok(val);
  }

  void UpdateNext(T id) TA_REQ(mutex_) {
    next_ = static_cast<T>((id + 1u) % MaxId);
    if (next_ == 0) {
      next_ = MinId;
    }
  }

  DECLARE_MUTEX(IdAllocator) mutex_;
  T next_ TA_GUARDED(mutex_) = MinId;
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<MaxId>> bitmap_ TA_GUARDED(mutex_);
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_ID_ALLOCATOR_H_
