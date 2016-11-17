// Copyright 2016 The Fuchisa Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MONITOR_H
#define MONITOR_H

#include "macros.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace magma {

// A Monitor provides explicit Acquire/Release mutex semantics together with a condition variable.
// This is useful for managing shared state together with signalling.
class Monitor {
public:
    // Lock takes a shared_ptr to a monitor in order to keep the underlying mutex alive.
    class Lock {
    public:
        Lock(std::shared_ptr<Monitor> monitor) : monitor_(std::move(monitor)) {}

        void Acquire()
        {
            DASSERT(!lock_.owns_lock());
            lock_ = std::unique_lock<std::mutex>(monitor_->mutex_);
        }

        void Release()
        {
            DASSERT(lock_.owns_lock());
            lock_.unlock();
        }

        std::unique_lock<std::mutex>& lock() { return lock_; }

        bool acquired(Monitor* monitor) { return monitor == monitor_.get() && lock_.owns_lock(); }

    private:
        std::shared_ptr<Monitor> monitor_;
        std::unique_lock<std::mutex> lock_;
    };

    static std::shared_ptr<Monitor> CreateShared()
    {
        return std::shared_ptr<Monitor>(new Monitor());
    }

    void Signal() { cv_.notify_one(); }

    // Wait until the condition associated with the monitor has occurred.
    // NOTE: may return early due to spurious wakeups in the underlying condition variable.  So this
    // must be called
    // in a loop while checking some other state.
    // The thread calling this function must already have entered the monitor.
    void Wait(Lock* lock)
    {
        DASSERT(lock->acquired(this));
        cv_.wait(lock->lock());
    }

    // Similar to |Wait| but also returns if the given time point is reached, in which case
    // |timedout_out| will be set to true.
    void WaitUntil(Lock* lock, std::chrono::high_resolution_clock::time_point time_point,
                   bool* timedout_out)
    {
        DASSERT(lock->acquired(this));
        *timedout_out = (cv_.wait_until(lock->lock(), time_point) == std::cv_status::timeout);
    }

private:
    Monitor() {}

    std::mutex mutex_;
    std::condition_variable cv_;

    DISALLOW_COPY_AND_ASSIGN(Monitor);
};

} // namespace magma

#endif // MONITOR_H
