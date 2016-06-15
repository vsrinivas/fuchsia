// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <runtime/mutex.h>

#include <magenta/syscalls.h>
#include <runtime/atomic.h>
#include <stdlib.h>

#include <stdio.h>

// TODO(kulakowski) Reintroduce (correctly) optimization counting waiters.

// These values have to be as such. UNLOCKED == 0 allows locks to be
// statically allocated, and the ordering of the values is relied upon
// in the atomic decrement in the unlock routine.
enum {
    UNLOCKED = 0,
    LOCKED = 1,
};

mx_status_t mxr_mutex_trylock(mxr_mutex_t* mutex) {
    int futex_value = atomic_cmpxchg(&mutex->futex, UNLOCKED, LOCKED);
    if (futex_value != UNLOCKED)
        return ERR_BUSY;
    return NO_ERROR;
}

mx_status_t mxr_mutex_timedlock(mxr_mutex_t* mutex, mx_time_t timeout) {
    for (;;) {
        switch (__atomic_exchange_n(&mutex->futex, LOCKED, __ATOMIC_SEQ_CST)) {
        case UNLOCKED:
            return NO_ERROR;
        case LOCKED: {
            mx_status_t status = _magenta_futex_wait(&mutex->futex, LOCKED, timeout);
            if (status == ERR_BUSY) {
                continue;
            }
            if (status != NO_ERROR) {
                return status;
            }
            continue;
        }
        }
    }
}

void mxr_mutex_lock(mxr_mutex_t* mutex) {
    mx_status_t status = mxr_mutex_timedlock(mutex, MX_TIME_INFINITE);
    if (status != NO_ERROR)
        abort();
}

void mxr_mutex_unlock(mxr_mutex_t* mutex) {
    __atomic_store_n(&mutex->futex, UNLOCKED, __ATOMIC_SEQ_CST);
    mx_status_t status = _magenta_futex_wake(&mutex->futex, 0x7FFFFFFF);
    if (status != NO_ERROR)
        abort();
}
