// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/futex_context.h>

#include <assert.h>
#include <lib/user_copy/user_ptr.h>
#include <fbl/auto_lock.h>
#include <object/thread_dispatcher.h>
#include <trace.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

FutexContext::FutexContext() {
    LTRACE_ENTRY;
}

FutexContext::~FutexContext() {
    LTRACE_ENTRY;

    // All of the threads should have removed themselves from wait queues
    // by the time the process has exited.
    DEBUG_ASSERT(futex_table_.is_empty());
}

mx_status_t FutexContext::FutexWait(user_ptr<int> value_ptr, int current_value, mx_time_t deadline) {
    LTRACE_ENTRY;

    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr.get());
    if (futex_key % sizeof(int))
        return MX_ERR_INVALID_ARGS;

    FutexNode* node;

    // FutexWait() checks that the address value_ptr still contains
    // current_value, and if so it sleeps awaiting a FutexWake() on value_ptr.
    // Those two steps must together be atomic with respect to FutexWake().
    // If a FutexWake() operation could occur between them, a userland mutex
    // operation built on top of futexes would have a race condition that
    // could miss wakeups.
    lock_.Acquire();

    int value;
    mx_status_t result = value_ptr.copy_from_user(&value);
    if (result != MX_OK) {
        lock_.Release();
        return result;
    }
    if (value != current_value) {
        lock_.Release();
        return MX_ERR_BAD_STATE;
    }

    ThreadDispatcher* thread = ThreadDispatcher::GetCurrent();
    node = thread->futex_node();
    node->set_hash_key(futex_key);
    node->SetAsSingletonList();

    QueueNodesLocked(node);

    // Block current thread.  This releases lock_ and does not reacquire it.
    result = node->BlockThread(&lock_, deadline);
    if (result == MX_OK) {
        DEBUG_ASSERT(!node->IsInQueue());
        // All the work necessary for removing us from the hash table was done by FutexWake()
        return MX_OK;
    }

    // The following happens if we hit the deadline (MX_ERR_TIMED_OUT) or if
    // the thread was killed (MX_ERR_INTERNAL_INTR_KILLED) or suspended
    // (MX_ERR_INTERNAL_INTR_RETRY).
    //
    // We need to ensure that the thread's node is removed from the wait
    // queue, because FutexWake() probably didn't do that.
    AutoLock lock(&lock_);
    if (UnqueueNodeLocked(node)) {
        return result;
    }
    // The current thread was not found on the wait queue.  This means
    // that, although we hit the deadline (or were suspended/killed), we
    // were *also* woken by FutexWake() (which removed the thread from the
    // wait queue) -- the two raced together.
    //
    // In this case, we want to return a success status.  This preserves
    // the property that if FutexWake() is called with wake_count=1 and
    // there are waiting threads, then at least one FutexWait() call
    // returns success.
    //
    // If that property is broken, it can lead to missed wakeups in
    // concurrency constructs that are built on top of futexes.  For
    // example, suppose a FutexWake() call from pthread_mutex_unlock()
    // races with a FutexWait() deadline from pthread_mutex_timedlock(). A
    // typical implementation of pthread_mutex_timedlock() will return
    // immediately without trying again to claim the mutex if this
    // FutexWait() call returns a timeout status.  If that happens, and if
    // another thread is waiting on the mutex, then that thread won't get
    // woken -- the wakeup from the FutexWake() call would have got lost.
    return MX_OK;
}

mx_status_t FutexContext::FutexWake(user_ptr<const int> value_ptr,
                                    uint32_t count) {
    LTRACE_ENTRY;

    if (count == 0) return MX_OK;

    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr.get());
    if (futex_key % sizeof(int))
        return MX_ERR_INVALID_ARGS;

    AutoLock lock(&lock_);

    FutexNode* node = futex_table_.erase(futex_key);
    if (!node) {
        // nothing blocked on this futex if we can't find it
        return MX_OK;
    }
    DEBUG_ASSERT(node->GetKey() == futex_key);

    bool any_woken = false;
    FutexNode* remaining_waiters =
        FutexNode::WakeThreads(node, count, futex_key, &any_woken);

    if (remaining_waiters) {
        DEBUG_ASSERT(remaining_waiters->GetKey() == futex_key);
        futex_table_.insert(remaining_waiters);
    }

    if (any_woken) {
        lock.release();
        thread_reschedule();
    }

    return MX_OK;
}

mx_status_t FutexContext::FutexRequeue(user_ptr<int> wake_ptr, uint32_t wake_count, int current_value,
                                       user_ptr<int> requeue_ptr, uint32_t requeue_count) {
    LTRACE_ENTRY;

    if ((requeue_ptr.get() == nullptr) && requeue_count)
        return MX_ERR_INVALID_ARGS;

    AutoLock lock(&lock_);

    int value;
    mx_status_t result = wake_ptr.copy_from_user(&value);
    if (result != MX_OK) return result;
    if (value != current_value) return MX_ERR_BAD_STATE;

    uintptr_t wake_key = reinterpret_cast<uintptr_t>(wake_ptr.get());
    uintptr_t requeue_key = reinterpret_cast<uintptr_t>(requeue_ptr.get());
    if (wake_key == requeue_key) return MX_ERR_INVALID_ARGS;
    if (wake_key % sizeof(int) || requeue_key % sizeof(int))
        return MX_ERR_INVALID_ARGS;

    // This must happen before RemoveFromHead() calls set_hash_key() on
    // nodes below, because operations on futex_table_ look at the GetKey
    // field of the list head nodes for wake_key and requeue_key.
    FutexNode* node = futex_table_.erase(wake_key);
    if (!node) {
        // nothing blocked on this futex if we can't find it
        return MX_OK;
    }

    bool any_woken = false;
    if (wake_count > 0) {
        node = FutexNode::WakeThreads(node, wake_count, wake_key, &any_woken);
    }

    // node is now the head of wake_ptr futex after possibly removing some threads to wake
    if (node != nullptr) {
        if (requeue_count > 0) {
            // head and tail of list of nodes to requeue
            FutexNode* requeue_head = node;
            node = FutexNode::RemoveFromHead(node, requeue_count,
                                             wake_key, requeue_key);

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

    if (any_woken) {
        lock.release();
        thread_reschedule();
    }

    return MX_OK;
}

void FutexContext::QueueNodesLocked(FutexNode* head) {
    DEBUG_ASSERT(lock_.IsHeld());

    FutexNode::HashTable::iterator iter;

    // Attempt to insert this FutexNode into the hash table.  If the insert
    // succeeds, then the current thread is first to block on this futex and we
    // are finished.  If the insert fails, then there is already a thread
    // waiting on this futex.  Add ourselves to that thread's list.
    if (!futex_table_.insert_or_find(head, &iter))
        iter->AppendList(head);
}

// This attempts to unqueue a thread (which may or may not be waiting on a
// futex), given its FutexNode.  This returns whether the FutexNode was
// found and removed from a futex wait queue.
bool FutexContext::UnqueueNodeLocked(FutexNode* node) {
    DEBUG_ASSERT(lock_.IsHeld());

    if (!node->IsInQueue())
        return false;

    // Note: When UnqueueNode() is called from FutexWait(), it might be
    // tempting to reuse the futex key that was passed to FutexWait().
    // However, that could be out of date if the thread was requeued by
    // FutexRequeue(), so we need to re-get the hash table key here.
    uintptr_t futex_key = node->GetKey();

    FutexNode* old_head = futex_table_.erase(futex_key);
    DEBUG_ASSERT(old_head);
    FutexNode* new_head = FutexNode::RemoveNodeFromList(old_head, node);
    if (new_head)
        futex_table_.insert(new_head);
    return true;
}
