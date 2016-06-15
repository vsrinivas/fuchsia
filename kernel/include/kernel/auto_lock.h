// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>

class AutoLock {
public:
    AutoLock(mutex_t* mutex)
        :   mutex_(mutex) {
        mutex_acquire(mutex_);
    }

    AutoLock(mutex_t& mutex)
        :   AutoLock(&mutex) {}

    ~AutoLock() {
        release();
    }

    // early release the mutex before the object goes out of scope
    void release() {
        if (mutex_) {
            mutex_release(mutex_);
            mutex_ = nullptr;
        }
    }

    // suppress default constructors
    AutoLock(const AutoLock& am) = delete;
    AutoLock& operator=(const AutoLock& am) = delete;
    AutoLock(AutoLock&& c) = delete;
    AutoLock& operator=(AutoLock&& c) = delete;

private:
    mutex_t* mutex_;
};
