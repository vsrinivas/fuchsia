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
#include <utils/hash_table.h>

// Node for linked list of threads blocked on a futex
// Intended to be embedded within a UserThread Instance
class FutexNode {
public:
    FutexNode();
    ~FutexNode();

    // adds a list of nodes to our tail
    void AppendList(FutexNode* head);

    // remove up to |count| nodes from our head and return new head
    // the removed nodes remain a valid list after this operation
    FutexNode* RemoveFromHead(uint32_t count, uintptr_t old_hash_key,
                              uintptr_t new_hash_key);

    // block the current thread, releasing the given mutex while the thread
    // is blocked
    status_t BlockThread(mutex_t* mutex, mx_time_t timeout);

    // wakes the list of threads starting with node |head|
    static void WakeThreads(FutexNode* head);

    FutexNode* next() const {
        return next_;
    }
    void set_next(FutexNode* node) {
        next_ = node;
    }

    // for SinglyLinkedList entry in the FutexContext hash table
    FutexNode* list_next() {
        return hash_next_;
    }
    void list_set_next(FutexNode* node) {
        hash_next_ = node;
    }

    uintptr_t hash_key() const {
        return hash_key_;
    }
    void set_hash_key(uintptr_t key) {
        hash_key_ = key;
    }

    FutexNode* tail() const {
        return tail_;
    }
    void set_tail(FutexNode* tail) {
        tail_ = tail;
    }

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

    // for SinglyLinkedList entry in futex hash table
    FutexNode* hash_next_;

    // for list of threads blocked on a futex
    FutexNode* next_;

    // tail node of the node list
    // only valid if this node is the list head
    FutexNode* tail_;
};

uintptr_t GetHashTableKey(FutexNode* node);
void SetHashTableKey(FutexNode* node, uintptr_t key);
