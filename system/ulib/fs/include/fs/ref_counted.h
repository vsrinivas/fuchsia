// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/atomic.h>
#include <fbl/ref_counted_internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace fs {

// VnodeRefCounted implements a customized RefCounted object.
//
// This object acts nearly identically to the base "RefCounted" --
// all methods have the same implementation -- but it adds an additional
// method, "ResurrectRef", which allows Vnodes to be re-used
// after a reference count of zero has been reached.
template <typename T,
          bool EnableAdoptionValidator = ZX_DEBUG_ASSERT_IMPLEMENTED>
class VnodeRefCounted : public fbl::internal::RefCountedBase<EnableAdoptionValidator> {
public:
    constexpr VnodeRefCounted()
        : ref_count_(1) {}
    ~VnodeRefCounted() {}

    // Default methods.
    void AddRef() const {
        fbl::internal::AddRefDefault<EnableAdoptionValidator>(ref_count_,
                                                              adoption_validator_);
    }
    bool AddRefMaybeInDestructor() {
        return fbl::internal::AddRefMaybeInDestructorDefault(ref_count_);
    }
    bool Release() const __WARN_UNUSED_RESULT {
        return fbl::internal::ReleaseRefDefault<EnableAdoptionValidator>(ref_count_,
                                                                         adoption_validator_);
    }
    void Adopt() const {
        adoption_validator_.Adopt();
    }

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
        adoption_validator_.ValidateAddRef();
        if (EnableAdoptionValidator) {
            int old = ref_count_.load(fbl::memory_order_acquire);
            ZX_DEBUG_ASSERT_MSG(old == 0, "count %d != 0\n", old);
        }
        ref_count_.store(1, fbl::memory_order_release);
    }

private:
    mutable fbl::atomic_int ref_count_;
    fbl::internal::AdoptionValidator<EnableAdoptionValidator> adoption_validator_;

    // VnodeRefCounted<> instances may not be copied, assigned or moved.
    DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeRefCounted);
};

} // namespace fs
