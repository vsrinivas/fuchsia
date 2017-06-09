// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <mxtl/atomic.h>

namespace mxtl {
namespace internal {

template <bool Enabled>
class AdoptionValidator;

template <>
class AdoptionValidator<true> {
public:
    void Adopt() {
        MX_DEBUG_ASSERT(!adopted_);
        adopted_ = true;
    }
    void ValidateAddRef() {  MX_DEBUG_ASSERT(adopted_); }
    void ValidateRelease() { MX_DEBUG_ASSERT(adopted_); }

private:
    bool adopted_ = false;
};

template <>
class AdoptionValidator<false> {
public:
    void Adopt() { }
    void ValidateAddRef() { }
    void ValidateRelease() { }
};

template <bool EnableAdoptionValidator>
class RefCountedBase {
protected:
    constexpr RefCountedBase() : ref_count_(1) {}
    ~RefCountedBase() {}
    void AddRef() {
        adoption_validator_.ValidateAddRef();
        ref_count_.fetch_add(1, memory_order_relaxed);
    }
    // Returns true if the object should self-delete.
    bool Release() __WARN_UNUSED_RESULT {
        adoption_validator_.ValidateRelease();
        if (ref_count_.fetch_sub(1, memory_order_release) == 1) {
            atomic_thread_fence(memory_order_acquire);
            return true;
        }
        return false;
    }

    void Adopt() {
        adoption_validator_.Adopt();
    }

    // Current ref count. Only to be used for debugging purposes.
    int ref_count_debug() const {
        return ref_count_.load(memory_order_relaxed);
    }

private:
    mxtl::atomic_int ref_count_;
    AdoptionValidator<EnableAdoptionValidator> adoption_validator_;
};

}  // namespace internal
}  // namespace mxtl
