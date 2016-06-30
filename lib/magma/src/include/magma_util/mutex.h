// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MUTEX_H_
#define MUTEX_H_

#include "magma_util/macros.h"
#include <mutex>

namespace magma {

class Mutex : public std::mutex {
public:
    Mutex() : locked_(false) {}

    void lock()
    {
        DASSERT(!locked_);
        std::mutex::lock();
        locked_ = true;
    }
    void unlock()
    {
        DASSERT(locked_);
        std::mutex::unlock();
        locked_ = false;
    }
    bool try_lock()
    {
        DASSERT(!locked_);
        if (std::mutex::try_lock()) {
            locked_ = true;
            return true;
        }
        return false;
    }
    bool is_locked() { return locked_; }

private:
    bool locked_;
};

class LockGuard : public std::lock_guard<Mutex> {
public:
    LockGuard(Mutex& mutex) : std::lock_guard<Mutex>(mutex) {}
};

} // namespace magma

#endif // MUTEX_H_
