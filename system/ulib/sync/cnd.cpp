// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/cnd.h>
#include <lib/sync/internal/condvar-template.h>
#include <assert.h>

template <>
struct condvar_impl_internal::MutexOps<sync_mtx_t> {
    static zx_futex_t* get_futex(sync_mtx_t* mutex) {
        return &mutex->futex;
    }

    static zx_status_t lock(sync_mtx_t* mutex, int* mutex_lock_err) __TA_ACQUIRE(mutex) {
        sync_mtx_lock(mutex);
        return ZX_OK;
    }

    static zx_status_t lock_with_waiters(
        sync_mtx_t* mutex, int waiters_delta, int* mutex_lock_err) __TA_ACQUIRE(mutex) {
        sync_mtx_lock_with_waiter(mutex);
        return ZX_OK;
    }

    static void unlock(sync_mtx_t* mutex) __TA_RELEASE(mutex) {
        sync_mtx_unlock(mutex);
    }
};

void sync_cnd_wait(sync_cnd_t* condvar, sync_mtx_t* mutex) {
    zx_status_t status = condvar_impl_internal::timedwait(
        condvar, mutex, ZX_TIME_INFINITE, nullptr);
    assert(status == ZX_OK);
}

zx_status_t sync_cnd_timedwait(sync_cnd_t* condvar, sync_mtx_t* mutex, zx_time_t deadline) {
    return condvar_impl_internal::timedwait(condvar, mutex, deadline, nullptr);
}

void sync_cnd_signal(sync_cnd_t* condvar) {
    return condvar_impl_internal::signal(condvar, 1);
}

void sync_cnd_broadcast(sync_cnd_t* condvar) {
    condvar_impl_internal::signal(condvar, -1);
}
