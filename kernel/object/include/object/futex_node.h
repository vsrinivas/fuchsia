// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/thread.h>
#include <kernel/wait.h>
#include <list.h>
#include <magenta/types.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>

// Node for linked list of threads blocked on a futex
// Intended to be embedded within a ThreadDispatcher Instance
class FutexNode : public fbl::SinglyLinkedListable<FutexNode*> {
public:
    using HashTable = fbl::HashTable<uintptr_t, FutexNode*>;

    FutexNode();
    ~FutexNode();

    FutexNode(const FutexNode &) = delete;
    FutexNode& operator=(const FutexNode &) = delete;

    bool IsInQueue() const;
    void SetAsSingletonList();

    // adds a list of nodes to our tail
    void AppendList(FutexNode* head);

    static FutexNode* RemoveNodeFromList(FutexNode* list_head, FutexNode* node);

    static FutexNode* WakeThreads(FutexNode* node, uint32_t count,
                                  uintptr_t old_hash_key, bool* out_any_woken);

    static FutexNode* RemoveFromHead(FutexNode* list_head,
                                     uint32_t count,
                                     uintptr_t old_hash_key,
                                     uintptr_t new_hash_key);

    // This must be called with |mutex| held and returns without |mutex| held.
    mx_status_t BlockThread(fbl::Mutex* mutex, mx_time_t deadline) TA_REL(mutex);

    void set_hash_key(uintptr_t key) {
        hash_key_ = key;
    }

    // Trait implementation for fbl::HashTable
    uintptr_t GetKey() const { return hash_key_; }
    static size_t GetHash(uintptr_t key) { return (key >> 3); }

private:
    static void RelinkAsAdjacent(FutexNode* node1, FutexNode* node2);
    static void SpliceNodes(FutexNode* node1, FutexNode* node2);

    bool WakeThread();

    void MarkAsNotInQueue();

    // hash_key_ contains the futex address.  This field has two roles:
    //  * It is used by FutexWait() to determine which queue to remove the
    //    thread from when a wait operation times out.
    //  * Additionally, when this FutexNode is the head of a futex wait
    //    queue, this field is used by the HashTable (because it uses
    //    intrusive SinglyLinkedLists).
    uintptr_t hash_key_;

    // Used for waking the thread corresponding to the FutexNode.
    wait_queue_t wait_queue_;

    // queue_prev_ and queue_next_ are used for maintaining a circular
    // doubly-linked list of threads that are waiting on one futex address.
    //  * When the list contains only this node, queue_prev_ and
    //    queue_next_ both point back to this node.
    //  * When the thread is not waiting on a futex, queue_next_ is null.
    FutexNode* queue_prev_ = nullptr;
    FutexNode* queue_next_ = nullptr;
};
