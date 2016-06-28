// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <kernel/auto_lock.h>
#include <lib/user_copy.h>
#include <magenta/futex_context.h>
#include <magenta/user_copy.h>
#include <magenta/user_thread.h>
#include <trace.h>

#define LOCAL_TRACE 0

FutexContext::FutexContext() {
    LTRACE_ENTRY;

    mutex_init(&lock_);
}

FutexContext::~FutexContext() {
    LTRACE_ENTRY;

    mutex_destroy(&lock_);
}

status_t FutexContext::FutexWait(int* value_ptr, int current_value, mx_time_t timeout) {
    LTRACE_ENTRY;

    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr);
    FutexNode* node;

    // FutexWait() checks that the address value_ptr still contains
    // current_value, and if so it sleeps awaiting a FutexWake() on value_ptr.
    // Those two steps must together be atomic with respect to FutexWake().
    // If a FutexWake() operation could occur between them, a userland mutex
    // operation built on top of futexes would have a race condition that
    // could miss wakeups.
    AutoLock lock(lock_);
    int value;
    status_t result = magenta_copy_from_user(value_ptr, &value, sizeof(value));
    if (result != NO_ERROR) return result;
    if (value != current_value) return ERR_BUSY;

    node = UserThread::GetCurrent()->futex_node();
    node->set_hash_key(futex_key);
    node->set_next(nullptr);
    node->set_tail(node);

    QueueNodesLocked(futex_key, node);

    // Block current thread
    result = node->BlockThread(&lock_, timeout);
    if (result == NO_ERROR) {
        // All the work necessary for removing us from the hash table was be done by FutexWake()
        return NO_ERROR;
    }
    // If we got a timeout, we need to remove the thread's node from the
    // wait queue, since FutexWake() didn't do that.  We need to re-get the
    // hash table key, because it might have changed if the thread was
    // requeued by FutexRequeue().
    futex_key = node->hash_key();
    FutexNode* list_head = futex_table_.get(futex_key);
    FutexNode* test = list_head;
    FutexNode* prev = nullptr;
    while (test) {
        DEBUG_ASSERT(test->hash_key() == futex_key);
        FutexNode* next = test->next();
        if (test == node) {
            if (prev) {
                // unlink from linked list
                prev->set_next(next);
                if (!next) {
                    // We have removed the last element, so we need to
                    // update the tail pointer.
                    list_head->set_tail(prev);
                }
            } else {
                // reset head of futex
                futex_table_.remove(futex_key);
                if (next) {
                    next->set_tail(list_head->tail());
                    futex_table_.add(futex_key, next);
                }
            }
            return ERR_TIMED_OUT;
        }
        prev = test;
        test = next;
    }
    // The current thread was not found on the wait queue.  This means
    // that, although we got a timeout, we were *also* woken by FutexWake()
    // (which removed the thread from the wait queue) -- the two raced
    // together.
    //
    // In this case, we want to return a success status.  This preserves
    // the property that if FutexWake() is called with wake_count=1 and
    // there are waiting threads, then at least one FutexWait() call
    // returns success.
    //
    // If that property is broken, it can lead to missed wakeups in
    // concurrency constructs that are built on top of futexes.  For
    // example, suppose a FutexWake() call from pthread_mutex_unlock()
    // races with a FutexWait() timeout from pthread_mutex_timedlock().  A
    // typical implementation of pthread_mutex_timedlock() will return
    // immediately without trying again to claim the mutex if this
    // FutexWait() call returns a timeout status.  If that happens, and if
    // another thread is waiting on the mutex, then that thread won't get
    // woken -- the wakeup from the FutexWake() call would have got lost.
    return NO_ERROR;
}

status_t FutexContext::FutexWake(int* value_ptr, uint32_t count) {
    LTRACE_ENTRY;

    if (count == 0) return NO_ERROR;

    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr);

    {
        AutoLock lock(lock_);

        FutexNode* node = futex_table_.get(futex_key);
        if (!node) {
            // nothing blocked on this futex if we can't find it
            return NO_ERROR;
        }

        FutexNode* wake_head = node;
        node = node->RemoveFromHead(count, futex_key, futex_key);
        // node is now the new blocked thread list head

        futex_table_.remove(futex_key);
        if (node != nullptr) {
            // TODO - Add a HashTable::replace() and use that instead of removing and re-adding
            futex_table_.add(futex_key, node);
        }

        // Traversing this list of threads must be done while holding the
        // lock, because any of these threads might wake up from a timeout
        // and call FutexWait(), which would clobber the "next" pointer in
        // the thread's FutexNode.
        FutexNode::WakeThreads(wake_head);
    }

    return NO_ERROR;
}

status_t FutexContext::FutexRequeue(int* wake_ptr, uint32_t wake_count, int current_value,
                                    int* requeue_ptr, uint32_t requeue_count) {
    LTRACE_ENTRY;

    AutoLock lock(lock_);

    int value;
    status_t result = magenta_copy_from_user(wake_ptr, &value, sizeof(value));
    if (result != NO_ERROR) return result;
    if (value != current_value) return ERR_BUSY;

    uintptr_t wake_key = reinterpret_cast<uintptr_t>(wake_ptr);
    uintptr_t requeue_key = reinterpret_cast<uintptr_t>(requeue_ptr);
    if (wake_key == requeue_key) return ERR_INVALID_ARGS;

    FutexNode* node = futex_table_.get(wake_key);
    if (!node) {
        // nothing blocked on this futex if we can't find it
        return NO_ERROR;
    }

    // This must happen before RemoveFromHead() calls set_hash_key() on
    // nodes below, because operations on futex_table_ look at the hash_key
    // field of the list head nodes for wake_key and requeue_key.
    futex_table_.remove(wake_key);

    FutexNode* wake_head;
    if (wake_count == 0) {
        wake_head = nullptr;
    } else {
        wake_head = node;
        node = node->RemoveFromHead(wake_count, wake_key, wake_key);
    }

    // node is now the head of wake_ptr futex after possibly removing some threads to wake
    if (node != nullptr) {
        if (requeue_count > 0) {
            // head and tail of list of nodes to requeue
            FutexNode* requeue_head = node;
            node = node->RemoveFromHead(requeue_count, wake_key, requeue_key);

            // now requeue our nodes to requeue_ptr mutex
            QueueNodesLocked(requeue_key, requeue_head);
        }
    }

    // add any remaining nodes back to wake_key futex
    if (node != nullptr) {
        futex_table_.add(wake_key, node);
    }

    FutexNode::WakeThreads(wake_head);

    return NO_ERROR;
}

void FutexContext::QueueNodesLocked(uintptr_t futex_key, FutexNode* head) {
    FutexNode* current_head = futex_table_.get(futex_key);
    if (!current_head) {
        // The current thread is first to block on this futex, so add it to the hash table.
        futex_table_.add(futex_key, head);
    } else {
        // push node for current thread at end of list
        current_head->AppendList(head);
    }
}

size_t FutexContext::FutexHashFn::operator()(uintptr_t key) const {
    // Futex address is likely 8 byte aligned, so ignore low 3 bits
    return static_cast<size_t>((key >> 3) % FUTEX_HASH_BUCKET_COUNT);
}
