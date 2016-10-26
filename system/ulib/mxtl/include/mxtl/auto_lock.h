// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#ifdef __cplusplus

#include <mxtl/macros.h>
#include <mxtl/mutex.h>

// Introduce preprocessor definitions for the underlying mutex data type and the
// lock/unlock operations based on whether this code is being used in the kernel
// or in user-mode.  Also, if this is the user-mode AutoLock implementation,
// introduce it into the mxtl namespace.  Otherwise, add it to the global
// namespace, and create an alias in mxtl.
#ifdef _KERNEL
#define mxtl_mutex_t mutex_t
#define mxtl_mutex_acquire mutex_acquire
#define mxtl_mutex_release mutex_release
#else
namespace mxtl {
#define mxtl_mutex_t mtx_t
#define mxtl_mutex_acquire mtx_lock
#define mxtl_mutex_release mtx_unlock
#endif

class AutoLock {
public:
    explicit AutoLock(mxtl_mutex_t* mutex)
        :   mutex_(mutex) {
        mxtl_mutex_acquire(mutex_);
    }

    explicit AutoLock(mxtl_mutex_t& mutex)
        :   AutoLock(&mutex) {}

    explicit AutoLock(Mutex& mutex)
        :   AutoLock(mutex.GetInternal()) {}

    explicit AutoLock(Mutex* mutex)
        :   AutoLock(mutex->GetInternal()) {}

    ~AutoLock() {
        release();
    }

    // early release the mutex before the object goes out of scope
    void release() {
        if (mutex_) {
            mxtl_mutex_release(mutex_);
            mutex_ = nullptr;
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoLock);

private:
    mxtl_mutex_t* mutex_;
};

#if _KERNEL
namespace mxtl { using AutoLock = ::AutoLock; }
#else
}  // namespace mxtl
#endif

// Remove the underlying mutex preprocessor definitions.  Do not let them leak
// out into the world at large.
#undef mxtl_mutex_t
#undef mxtl_mutex_acquire
#undef mxtl_mutex_release

#endif  // #ifdef __cplusplus
