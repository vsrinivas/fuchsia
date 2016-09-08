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
}

FutexContext::~FutexContext() {
    LTRACE_ENTRY;
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

    UserThread* t = UserThread::GetCurrent();
    if (t->state() == UserThread::State::DYING || t->state() == UserThread::State::DEAD)
        return ERR_BAD_STATE;

    int value;
    status_t result = magenta_copy_from_user(value_ptr, &value, sizeof(value));
    if (result != NO_ERROR) return result;
    if (value != current_value) return ERR_BAD_STATE;

    node = t->futex_node();
    node->set_hash_key(futex_key);
    node->set_next(nullptr);
    node->set_tail(node);

    QueueNodesLocked(node);

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
    futex_key = node->GetKey();
    auto list_head_iter = futex_table_.find(futex_key);
    if (list_head_iter.IsValid()) {
        FutexNode& list_head = *list_head_iter;
        FutexNode* test = &list_head;
        FutexNode* prev = nullptr;
        while (test) {
            DEBUG_ASSERT(test->GetKey() == futex_key);
            FutexNode* next = test->next();
            if (test == node) {
                if (prev) {
                    // unlink from linked list
                    prev->set_next(next);
                    if (!next) {
                        // We have removed the last element, so we need to
                        // update the tail pointer.
                        list_head.set_tail(prev);
                    }
                } else {
                    // reset head of futex
                    futex_table_.erase(futex_key);
                    if (next) {
                        next->set_tail(list_head.tail());
                        DEBUG_ASSERT(next->GetKey() == futex_key);
                        futex_table_.insert(next);
                    }
                }
                return ERR_TIMED_OUT;
            }
            prev = test;
            test = next;
        }
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

void FutexContext::WakeAll() {
    LTRACE_ENTRY;

    AutoLock lock(lock_);
    for(auto &entry : futex_table_) {
        FutexNode::WakeThreads(&entry);
    }
    futex_table_.clear();
}

status_t FutexContext::FutexWake(int* value_ptr, uint32_t count) {
    LTRACE_ENTRY;

    if (count == 0) return NO_ERROR;

    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr);

    {
        AutoLock lock(lock_);

        FutexNode* node = futex_table_.erase(futex_key);
        if (!node) {
            // nothing blocked on this futex if we can't find it
            return NO_ERROR;
        }
        DEBUG_ASSERT(node->GetKey() == futex_key);

        FutexNode* wake_head = node;
        node = node->RemoveFromHead(count, futex_key, 0u);
        // node is now the new blocked thread list head

        if (node != nullptr) {
            DEBUG_ASSERT(node->GetKey() == futex_key);
            futex_table_.insert(node);
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

    if ((requeue_ptr == nullptr) && requeue_count)
        return ERR_INVALID_ARGS;

    AutoLock lock(lock_);

    int value;
    status_t result = magenta_copy_from_user(wake_ptr, &value, sizeof(value));
    if (result != NO_ERROR) return result;
    if (value != current_value) return ERR_BAD_STATE;

    uintptr_t wake_key = reinterpret_cast<uintptr_t>(wake_ptr);
    uintptr_t requeue_key = reinterpret_cast<uintptr_t>(requeue_ptr);
    if (wake_key == requeue_key) return ERR_INVALID_ARGS;

    // This must happen before RemoveFromHead() calls set_hash_key() on
    // nodes below, because operations on futex_table_ look at the GetKey
    // field of the list head nodes for wake_key and requeue_key.
    FutexNode* node = futex_table_.erase(wake_key);
    if (!node) {
        // nothing blocked on this futex if we can't find it
        return NO_ERROR;
    }

    FutexNode* wake_head;
    if (wake_count == 0) {
        wake_head = nullptr;
    } else {
        wake_head = node;
        node = node->RemoveFromHead(wake_count, wake_key, 0u);
    }

    // node is now the head of wake_ptr futex after possibly removing some threads to wake
    if (node != nullptr) {
        if (requeue_count > 0) {
            // head and tail of list of nodes to requeue
            FutexNode* requeue_head = node;
            node = node->RemoveFromHead(requeue_count, wake_key, requeue_key);

            // now requeue our nodes to requeue_ptr mutex
            DEBUG_ASSERT(requeue_head->GetKey() == requeue_key);
            QueueNodesLocked(requeue_head);
        }
    }

    // add any remaining nodes back to wake_key futex
    if (node != nullptr) {
        DEBUG_ASSERT(node->GetKey() == wake_key);
        futex_table_.insert(node);
    }

    FutexNode::WakeThreads(wake_head);
    return NO_ERROR;
}

void FutexContext::QueueNodesLocked(FutexNode* head) {
    FutexNode::HashTable::iterator iter;

    // Attempt to insert this FutexNode into the hash table.  If the insert
    // succeeds, then the current thread is first to block on this futex and we
    // are finished.  If the insert fails, then there is already a thread
    // waiting on this futex.  Add ourselves to that thread's list.
    if (!futex_table_.insert_or_find(head, &iter))
        iter->AppendList(head);
}
