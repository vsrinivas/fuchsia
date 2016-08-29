// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/cond.h>
#include <kernel/mutex.h>
#include <kernel/wait.h>
#include <list.h>
#include <magenta/types.h>
#include <mxtl/intrusive_hash_table.h>

// Node for linked list of threads blocked on a futex
// Intended to be embedded within a UserThread Instance
class FutexNode : public mxtl::SinglyLinkedListable<FutexNode*> {
public:
    using HashTable = mxtl::HashTable<uintptr_t, FutexNode*>;

    FutexNode();
    ~FutexNode();

    FutexNode(const FutexNode &) = delete;
    FutexNode& operator=(const FutexNode &) = delete;

    // adds a list of nodes to our tail
    void AppendList(FutexNode* head);

    // remove up to |count| nodes from our head and return new head
    // the removed nodes remain a valid list after this operation
    FutexNode* RemoveFromHead(uint32_t count, uintptr_t old_hash_key,
                              uintptr_t new_hash_key);

    // block the current thread, releasing the given mutex while the thread
    // is blocked
    status_t BlockThread(Mutex* mutex, mx_time_t timeout);

    // wakes the list of threads starting with node |head|
    static void WakeThreads(FutexNode* head);

    FutexNode* next() const {
        return next_;
    }

    void set_next(FutexNode* node) {
        next_ = node;
    }

    FutexNode* tail() const {
        return tail_;
    }

    void set_tail(FutexNode* tail) {
        tail_ = tail;
    }

    void set_hash_key(uintptr_t key) {
        hash_key_ = key;
    }

    // Trait implementation for mxtl::HashTable
    uintptr_t GetKey() const { return hash_key_; }
    static size_t GetHash(uintptr_t key) { return (key >> 3); }

private:
    // hash_key_ contains the futex address.  This field has two roles:
    //  * It is used by FutexWait() to determine which queue to remove the
    //    thread from when a wait operation times out.
    //  * Additionally, when this FutexNode is the head of a futex wait
    //    queue, this field is used by the HashTable (because it uses
    //    intrusive SinglyLinkedLists).
    uintptr_t hash_key_;

    // condition variable used for blocking our containing thread on
    cond_t condvar_;

    // for list of threads blocked on a futex
    FutexNode* next_;

    // tail node of the node list
    // only valid if this node is the list head
    FutexNode* tail_;
};
