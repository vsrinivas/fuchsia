// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted_upgradeable.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>

namespace fs {

// VnodeRefCounted implements a customized RefCounted object.
//
// It adds an additional method, "ResurrectRef", which allows Vnodes to be
// re-used after a reference count of zero has been reached.
template <typename T, bool EnableAdoptionValidator = ZX_DEBUG_ASSERT_IMPLEMENTED>
class VnodeRefCounted
    : private ::fbl::internal::RefCountedUpgradeableBase<EnableAdoptionValidator> {
 public:
  constexpr VnodeRefCounted() = default;
  ~VnodeRefCounted() = default;

  using ::fbl::internal::RefCountedBase<EnableAdoptionValidator>::AddRef;
  using ::fbl::internal::RefCountedBase<EnableAdoptionValidator>::Release;
  using ::fbl::internal::RefCountedBase<EnableAdoptionValidator>::Adopt;
  using ::fbl::internal::RefCountedBase<EnableAdoptionValidator>::ref_count_debug;

  // Don't use this method. See the relevant RefPtr implementation for details.
  using ::fbl::internal::RefCountedUpgradeableBase<
      EnableAdoptionValidator>::AddRefMaybeInDestructor;

  // VnodeRefCounted<> instances may not be copied, assigned or moved.
  DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeRefCounted);

  // This method should only be called if the refcount was "zero", implying the
  // object is currently executing fbl_recycle. In this case, the refcount
  // is increased by one.
  //
  // This method may be called to prevent fbl_recycle from following the
  // typical path of object deletion: instead of destroying the object,
  // this function can be called to "reset" the lifecycle of the RefCounted
  // object to the initialized state of "ref_count_ = 1", so it can
  // continue to be utilized after there are no strong references.
  //
  // This function should be used EXCLUSIVELY from within fbl_recycle.
  // If other clients (outside fbl_recycle) attempt to resurrect the Vnode
  // concurrently with a call to Vnode::fbl_recycle, they risk going through
  // the entire Vnode lifecycle and destroying it (with another call to
  // Vnode::fbl_recycle) before the initial recycle execution terminates.
  void ResurrectRef() const {
    if (EnableAdoptionValidator) {
      int32_t old = this->ref_count_.load(std::memory_order_relaxed);
      ZX_DEBUG_ASSERT_MSG(old == 0, "count %d(0x%08x) != 0\n", old, old);
    }
    this->ref_count_.store(1, std::memory_order_relaxed);
  }
};

}  // namespace fs
