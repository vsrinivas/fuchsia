// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_CONDITION_H_
#define LIB_SYNC_CONDITION_H_

#include <lib/sync/mutex.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// A condition variable that works with a sync_mutex_t
typedef struct sync_condition {
    int lock;
    void* head;
    void* tail;

#ifdef __cplusplus
    sync_condition()
        : lock(0), head(nullptr), tail(nullptr) {}
#endif
} sync_condition_t;

#if !defined(__cplusplus)
#define SYNC_CONDITION_INIT ((sync_condition_t){0})
#endif

// Block until |condition| is signaled by sync_condition_signal()/sync_condition_broadcast(), or a spurious
// wake up occurs.
//
// |mutex| must be in a locked state, and will be atomically unlocked for the duration of the wait,
// then locked again before the function returns.
void sync_condition_wait(sync_condition_t* condition, sync_mutex_t* mutex);

// Block until |condition| is signaled by sync_condition_signal()/sync_condition_broadcast(), or a spurious
// wake up or a timeout occurs.
//
// |mutex| must be in a locked state, and will be atomically unlocked for the duration of the wait,
// then locked again before the function returns.
//
// ZX_TIME_INFINITE can be used for |deadline| to wait for an unlimited amount of time.
//
// Return value:
//      ZX_OK if |condition| was signaled or a spurious wake up occurred.
//      ZX_ERR_TIMED_OUT if the wait timed out.
zx_status_t sync_condition_timedwait(sync_condition_t* condition, sync_mutex_t* mutex, zx_time_t deadline);

// Wake up one thread waiting for |condition|.
//
// If the woken thread was waiting on sync_condition_timedwait(), then it is guaranteed
// to receive a ZX_OK return value even if a race with a timeout occurs. As an example
// where this makes a difference, consider the following implementation of a multi-producer,
// multi-consumer queue:
//
// Message* MessageQueue::DequeueTimeout(zx_time_t deadline) {
//    sync_mutex_lock(&mutex_);
//    for (;;) {
//        if (!list_.empty()) {
//            Message* msg = list_.front();
//            list_.pop_front();
//            sync_mutex_unlock(&mutex_);
//            return msg;
//        }
//        zx_status_t status = sync_condition_timedwait(&condition_, &mutex_, deadline);
//        if (status == ZX_ERR_TIMED_OUT) {
//            // Without the above guarantee, this would be a bug: a race between
//            // a timeout and a signal() would result in a missed wakeup.
//            // To fix that, we would need to recheck list_.empty() here, which
//            // is not obvious and would make the code more complex.
//            sync_mutex_unlock(&mutex_);
//            return nullptr;
//        }
//    }
// }
//
// void MessageQueue::Enqueue(Message* msg) {
//     sync_mutex_lock(&mutex_);
//     list_.push_back(msg);
//     // Signal just one waiter. Assumes that any possible waiter will dequeue the message.
//     sync_condition_signal(&condvar_);
//     sync_mutex_unlock(&mutex_);
// }
//
// Note that pthread does not seem to require this property, and in fact the current upstream
// implementation of pthread_cond_timedwait() in MUSL does not have it.
void sync_condition_signal(sync_condition_t* condition);

// Wake up all threads that are currently waiting for |condition|.
void sync_condition_broadcast(sync_condition_t* condition);

__END_CDECLS

#endif // LIB_SYNC_CONDITION_H_
