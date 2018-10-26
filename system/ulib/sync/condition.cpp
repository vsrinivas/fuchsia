// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/condition.h>
#include <lib/sync/internal/condition-template.h>
#include <assert.h>

template <>
struct condition_impl_internal::MutexOps<sync_mutex_t> {
    static zx_futex_t* get_futex(sync_mutex_t* mutex) {
        return &mutex->futex;
    }

    static zx_status_t lock(sync_mutex_t* mutex, int* mutex_lock_err) __TA_ACQUIRE(mutex) {
        sync_mutex_lock(mutex);
        return ZX_OK;
    }

    static zx_status_t lock_with_waiters(
        sync_mutex_t* mutex, int waiters_delta, int* mutex_lock_err) __TA_ACQUIRE(mutex) {
        sync_mutex_lock_with_waiter(mutex);
        return ZX_OK;
    }

    static void unlock(sync_mutex_t* mutex) __TA_RELEASE(mutex) {
        sync_mutex_unlock(mutex);
    }
};

void sync_condition_wait(sync_condition_t* condition, sync_mutex_t* mutex) {
    zx_status_t status = condition_impl_internal::timedwait(
        condition, mutex, ZX_TIME_INFINITE, nullptr);
    assert(status == ZX_OK);
}

zx_status_t sync_condition_timedwait(sync_condition_t* condition, sync_mutex_t* mutex, zx_time_t deadline) {
    return condition_impl_internal::timedwait(condition, mutex, deadline, nullptr);
}

void sync_condition_signal(sync_condition_t* condition) {
    return condition_impl_internal::signal(condition, 1);
}

void sync_condition_broadcast(sync_condition_t* condition) {
    condition_impl_internal::signal(condition, -1);
}
