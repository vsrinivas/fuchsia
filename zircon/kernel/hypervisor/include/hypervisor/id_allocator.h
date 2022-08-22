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

using GenType = uint32_t;

template <typename T>
class [[nodiscard]] Id {
 public:
  static_assert(ktl::is_unsigned_v<T>, "T must be unsigned");

  Id(T val, GenType gen) : val_(val), gen_(gen) {}

  Id(Id&&) noexcept = default;
  Id& operator=(Id&&) noexcept = default;
  Id(const Id&) = delete;
  Id& operator=(const Id&) = delete;

  T val() const { return val_; }
  GenType gen() const { return gen_; }

 private:
  T val_;
  GenType gen_;
};

// Allocates architecture-specific resource IDs.
//
// IDs of type `T` will be allocated in the range [`MinId`, `MaxId`).
//
// If `Alloc` is used to allocate an ID, then an ID is guaranteed to be
// allocated. To do this, IDs are allocated and assigned a generation. If no IDs
// are available, then the generation count is increment and all IDs become
// available again.
//
// To ensure an ID is valid, before an operation that relies on the ID is
// attempted, `Migrate` should be called on the ID.
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

  // Resets the allocator, and sets a new `max_id`, where `max_id` <= `MaxId`.
  zx::status<> Reset(T max_id = MaxId) {
    if (max_id <= MinId) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    Guard<Mutex> lock{&mutex_};
    zx_status_t status = bitmap_.Reset(max_id);
    return zx::make_status(status);
  }

  // Allocate an ID, potentially within a new generation.
  Id<T> Alloc() {
    Guard<Mutex> lock{&mutex_};
    auto id = AllocFromHint(next_, /*use_gen*/ true);
    UpdateNext(*id);
    return std::move(*id);
  }

  // Try to allocate an ID within the current generation.
  zx::status<Id<T>> TryAlloc() {
    Guard<Mutex> lock{&mutex_};
    auto id = AllocFromHint(next_, /*use_gen*/ false);
    if (id.is_ok()) {
      UpdateNext(*id);
    }
    return id;
  }

  zx::status<> Free(Id<T> id) {
    // If the generations do not match, return as we have nothing to do.
    if (id.gen() != gen_) {
      return zx::ok();
    }
    if (!InRange(id.val())) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    Guard<Mutex> lock{&mutex_};
    if (!bitmap_.GetOne(id.val())) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    zx_status_t status = bitmap_.ClearOne(id.val());
    return zx::make_status(status);
  }

  // Migrate `id` to the latest generation. If `id` was not at the latest
  // generation, then `invalidate` will be called.
  template <typename F>
  void Migrate(Id<T>& id, F invalidate) {
    T last_val = id.val();
    GenType last_gen = id.gen();
    // If the generations match, or if the value is out of range, return as we
    // have nothing to do.
    if (last_gen == gen_ || !InRange(last_val)) {
      return;
    }
    {
      Guard<Mutex> lock{&mutex_};
      // Reallocate a new `id`, and execute the invalidation function.
      id = *AllocFromHint(id.val(), /*use_gen*/ true);
    }
    // If `id` has the same value and was only one generation behind, we can
    // safely skip the invalidation.
    if (last_val != id.val() || last_gen + 1u != id.gen()) {
      invalidate(id.val());
    }
  }

 private:
  bool InRange(T val) const { return val >= MinId && val < MaxId; }

  zx::status<Id<T>> AllocFromHint(T next, bool use_gen) TA_REQ(mutex_) {
    size_t first_unset;
  retry:
    if (bitmap_.Get(next, MaxId, &first_unset)) {
      if (bitmap_.Get(MinId, next, &first_unset)) {
        if (use_gen) {
          // There are no more free IDs in this generation, so increment the
          // generation and start again.
          ++gen_;
          bitmap_.Reset(bitmap_.size());
          goto retry;
        }
        return zx::error(ZX_ERR_NO_RESOURCES);
      }
    }
    zx_status_t status = bitmap_.SetOne(first_unset);
    // The bitmap returned this index as unset, therefore this should not fail.
    ZX_DEBUG_ASSERT(status == ZX_OK);
    auto val = static_cast<T>(first_unset);
    // The value should be in [MinId, MaxId), therefore this should not fail.
    ZX_DEBUG_ASSERT(val >= MinId && val < MaxId);
    return zx::ok(Id{val, gen_});
  }

  void UpdateNext(Id<T>& id) TA_REQ(mutex_) {
    next_ = static_cast<T>((id.val() + 1u) % MaxId);
    if (next_ == 0) {
      next_ = MinId;
    }
  }

  ktl::atomic<GenType> gen_ = 0;
  DECLARE_MUTEX(IdAllocator) mutex_;
  T next_ TA_GUARDED(mutex_) = MinId;
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<MaxId>> bitmap_ TA_GUARDED(mutex_);
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_ID_ALLOCATOR_H_
