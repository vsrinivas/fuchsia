// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <mxtl/atomic.h>

namespace mxtl {
namespace internal {

class RefCountedBase {
protected:
    constexpr RefCountedBase() : ref_count_(1) {}
    ~RefCountedBase() {}
    void AddRef() {
        MX_DEBUG_ASSERT_COND(adopted_);
        // TODO(jamesr): Replace uses of GCC builtins with something safer.
        ref_count_.fetch_add(1, memory_order_relaxed);
    }
    // Returns true if the object should self-delete.
    bool Release() __WARN_UNUSED_RESULT {
        MX_DEBUG_ASSERT_COND(adopted_);
        if (ref_count_.fetch_sub(1, memory_order_release) == 1) {
            atomic_thread_fence(memory_order_acquire);
            return true;
        }
        return false;
    }

#if MX_DEBUG_ASSERT_IMPLEMENTED
    void Adopt() {
        MX_DEBUG_ASSERT(!adopted_);
        adopted_ = true;
    }
#endif

    // Current ref count. Only to be used for debugging purposes.
    int ref_count_debug() const {
        return ref_count_.load(memory_order_relaxed);
    }

private:
    mxtl::atomic_int ref_count_;
#if MX_DEBUG_ASSERT_IMPLEMENTED
    bool adopted_ = false;
#endif
};

}  // namespace internal
}  // namespace mxtl
