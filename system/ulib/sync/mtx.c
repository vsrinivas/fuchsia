// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/mtx.h>

#include <runtime/mutex.h>

void sync_mtx_lock(sync_mtx_t* m) __TA_NO_THREAD_SAFETY_ANALYSIS {
    zxr_mutex_lock((zxr_mutex_t*)&m->futex);
}

zx_status_t sync_mtx_timedlock(sync_mtx_t* m, zx_time_t deadline) {
    return __zxr_mutex_timedlock((zxr_mutex_t*)&m->futex, deadline);
}

zx_status_t sync_mtx_trylock(sync_mtx_t* m) {
    return zxr_mutex_trylock((zxr_mutex_t*)&m->futex);
}

void sync_mtx_unlock(sync_mtx_t* m) __TA_NO_THREAD_SAFETY_ANALYSIS {
    zxr_mutex_unlock((zxr_mutex_t*)&m->futex);
}
