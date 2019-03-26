// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/defines.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <kernel/deadline.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <list.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

extern spin_lock_t thread_lock;

// wait queue stuff
#define WAIT_QUEUE_MAGIC (0x77616974) // 'wait'

typedef struct wait_queue {
    int magic;
    int count;
    struct list_node heads;
} wait_queue_t;

#define WAIT_QUEUE_INITIAL_VALUE(q)             \
    {                                           \
        .magic = WAIT_QUEUE_MAGIC,              \
        .count = 0,                             \
        .heads = LIST_INITIAL_VALUE((q).heads), \
    }

// When blocking this enum indicates the kind of resource ownership that is being waited for
// that is causing the block.
enum class ResourceOwnership {
    // Blocking is either not for any particular resource, or it is to wait for
    // exclusive access to a resource.
    Normal,
    // Blocking is happening whilst waiting for shared read access to a resource.
    Reader,
};

// wait queue primitive
// NOTE: must be inside critical section when using these
void wait_queue_init(wait_queue_t* wait);

void wait_queue_destroy(wait_queue_t*);

// block on a wait queue.
// return status is whatever the caller of wait_queue_wake_*() specifies.
// a deadline other than ZX_TIME_INFINITE will abort at the specified time
// and return ZX_ERR_TIMED_OUT. a deadline in the past will immediately return.

zx_status_t wait_queue_block(wait_queue_t*, zx_time_t deadline) TA_REQ(thread_lock);

// block on a wait queue, ignoring existing signals in |signal_mask|.
// return status is whatever the caller of wait_queue_wake_*() specifies or
// ZX_ERR_TIMED_OUT if the deadline has elapsed or is in the past.
// will never timeout when called with a deadline of ZX_TIME_INFINITE.
zx_status_t wait_queue_block_etc(wait_queue_t*,
                                 const Deadline& deadline,
                                 uint signal_mask,
                                 ResourceOwnership reason) TA_REQ(thread_lock);

// returns the highest priority of all the blocked threads on this wait queue.
// returns -1 if no threads are blocked.

int wait_queue_blocked_priority(const wait_queue_t*) TA_REQ(thread_lock);

// returns the current highest priority blocked thread on this wait queue, or
// null if no threads are blocked.
struct thread* wait_queue_peek(wait_queue_t*) TA_REQ(thread_lock);

// release one or more threads from the wait queue.
// reschedule = should the system reschedule if any is released.
// wait_queue_error = what wait_queue_block() should return for the blocking thread.

int wait_queue_wake_one(wait_queue_t*, bool reschedule,
                        zx_status_t wait_queue_error) TA_REQ(thread_lock);

int wait_queue_wake_all(wait_queue_t*, bool reschedule,
                        zx_status_t wait_queue_error) TA_REQ(thread_lock);
struct thread* wait_queue_dequeue_one(wait_queue_t* wait,
                                      zx_status_t wait_queue_error) TA_REQ(thread_lock);

// Move a single thread from the source wait queue to the destination wait
// queue, and return a pointer the the thread which was moved.  If there were
// no threads in the source wait queue, do nothing and return NULL.
struct thread* wait_queue_requeue_one(wait_queue_t* src, wait_queue_t* dst) TA_REQ(thread_lock);

// is the wait queue currently empty
bool wait_queue_is_empty(const wait_queue_t*) TA_REQ(thread_lock);

// remove a specific thread out of a wait queue it's blocked on
zx_status_t wait_queue_unblock_thread(struct thread* t,
                                      zx_status_t wait_queue_error) TA_REQ(thread_lock);

// a thread's priority has changed, potentially modify the wait queue it's in
void wait_queue_priority_changed(struct thread* t,
                                 int old_prio) TA_REQ(thread_lock);

// validate that the queue of a given wait queue is valid
void wait_queue_validate_queue(wait_queue_t* wait) TA_REQ(thread_lock);

__END_CDECLS

#ifdef __cplusplus

class WaitQueue {
public:
    WaitQueue() {}
    ~WaitQueue() { wait_queue_destroy(&wq_); }

    WaitQueue(WaitQueue&) = delete;
    WaitQueue(WaitQueue&&) = delete;
    WaitQueue& operator=(WaitQueue&) = delete;
    WaitQueue& operator=(WaitQueue&&) = delete;

    static struct thread* RequeueOne(WaitQueue* src, WaitQueue* dst) TA_REQ(thread_lock) {
        return wait_queue_requeue_one(&src->wq_, &dst->wq_);
    }

    zx_status_t Block(const Deadline& deadline) TA_REQ(thread_lock) {
        return wait_queue_block_etc(&wq_, deadline, 0, ResourceOwnership::Normal);
    }

    zx_status_t BlockReadLock(const Deadline& deadline) TA_REQ(thread_lock) {
        return wait_queue_block_etc(&wq_, deadline, 0, ResourceOwnership::Reader);
    }

    struct thread* Peek() TA_REQ(thread_lock) {
        return wait_queue_peek(&wq_);
    }

    int WakeOne(bool reschedule, zx_status_t wait_queue_error) TA_REQ(thread_lock) {
        return wait_queue_wake_one(&wq_, reschedule, wait_queue_error);
    }

    bool IsEmpty() TA_REQ(thread_lock) { return wait_queue_is_empty(&wq_); }

    static zx_status_t UnblockThread(struct thread* t, zx_status_t wait_queue_error)
        TA_REQ(thread_lock) {
        return wait_queue_unblock_thread(t, wait_queue_error);
    }

    struct thread* DequeueOne(zx_status_t wait_queue_error) TA_REQ(thread_lock) {
        return wait_queue_dequeue_one(&wq_, wait_queue_error);
    }

private:
    wait_queue_t wq_ = WAIT_QUEUE_INITIAL_VALUE(wq_);
};

#endif // __cplusplus
