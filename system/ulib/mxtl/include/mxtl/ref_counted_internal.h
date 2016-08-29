// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/compiler.h>

namespace mxtl {
namespace internal {

class RefCountedBase {
protected:
    constexpr RefCountedBase() {}
    ~RefCountedBase() {}
    void AddRef() {
        DEBUG_ASSERT(adopted_);
        // TODO(jamesr): Replace uses of GCC builtins with something safer.
        __atomic_fetch_add(&ref_count_, 1, __ATOMIC_RELAXED);
    }
    // Returns true if the object should self-delete.
    bool Release() __WARN_UNUSED_RESULT {
        DEBUG_ASSERT(adopted_);
        if (__atomic_fetch_add(&ref_count_, -1, __ATOMIC_RELEASE) == 1) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            return true;
        }
        return false;
    }

#if (LK_DEBUGLEVEL > 1)
    void Adopt() {
        DEBUG_ASSERT(!adopted_);
        adopted_ = true;
    }
#endif

    // Current ref count. Only to be used for debugging purposes.
    int ref_count_debug() const {
        return __atomic_load_n(&ref_count_, __ATOMIC_RELAXED);
    }

private:
    int ref_count_ = 1;
#if (LK_DEBUGLEVEL > 1)
    bool adopted_ = false;
#endif
};

}  // namespace internal
}  // namespace mxtl
