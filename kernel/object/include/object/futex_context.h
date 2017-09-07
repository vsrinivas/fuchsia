// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/user_copy/user_ptr.h>
#include <magenta/types.h>
#include <fbl/mutex.h>
#include <object/futex_node.h>

// FutexContext is a class that encapsulates support for futex operations.
// FutexContext uses a hash table keyed on the futex address (a pointer to integer in userspace)
// to contain all active futexes.
// A futex is considered active if there is one or more threads blocked on the futex.
// After no threads are left blocked on a futex it is removed from the hash table.
// The value in the futex hash table is the FutexNode object associated with the head
// of the list of threads blocked on the futex.
// To avoid memory allocation at futex operation time, a FutexNode is embedded in each
// ThreadDispatcher object.
// When the thread at the head of the futex's blocked thread list is resumed,
// The FutexNode for the new head of the blocked thread list is set as the hash table value
// for the futex.
class FutexContext {
public:
    FutexContext();
    ~FutexContext();

    // FutexWait first verifies that the integer pointed to by |value_ptr|
    // still equals |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
    // Otherwise it will block the current thread until the |deadline| passes,
    // or until the thread is woken by a FutexWake or FutexRequeue operation
    // on the same |value_ptr| futex.
    mx_status_t FutexWait(user_ptr<int> value_ptr, int current_value, mx_time_t deadline);

    // FutexWake will wake up to |count| number of threads blocked on the |value_ptr| futex.
    mx_status_t FutexWake(user_ptr<const int> value_ptr, uint32_t count);

    // FutexWait first verifies that the integer pointed to by |wake_ptr|
    // still equals |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
    // Otherwise it will wake up to |wake_count| number of threads blocked on the |wake_ptr| futex.
    // If any other threads remain blocked on on the |wake_ptr| futex, up to |requeue_count|
    // of them will then be requeued to the tail of the list of threads
    // blocked on the |requeue_ptr| futex.
    mx_status_t FutexRequeue(user_ptr<int> wake_ptr, uint32_t wake_count, int current_value,
                             user_ptr<int> requeue_ptr, uint32_t requeue_count);

private:
    FutexContext(const FutexContext&) = delete;
    FutexContext& operator=(const FutexContext&) = delete;

    void QueueNodesLocked(FutexNode* head) TA_REQ(lock_);

    bool UnqueueNodeLocked(FutexNode* node) TA_REQ(lock_);

    // protects futex_table_
    fbl::Mutex lock_;

    // Hash table for futexes in this context.
    // Key is futex address, value is the FutexNode for the head of futex's blocked thread list.
    FutexNode::HashTable futex_table_ TA_GUARDED(lock_);
};
