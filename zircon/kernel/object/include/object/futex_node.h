// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/lockdep.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <list.h>
#include <zircon/types.h>
#include <fbl/ref_ptr.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <object/thread_dispatcher.h>

// Node for linked list of threads blocked on a futex
//
// FutexNodes exist on the stack of each of the threads currently
// blocked on a given futex key.
//
class FutexNode : public fbl::SinglyLinkedListable<FutexNode*> {
public:
    using HashTable = fbl::HashTable<uintptr_t, FutexNode*>;

    enum class OwnerAction {
        RELEASE,
        ASSIGN_WOKEN,
    };

    explicit FutexNode(fbl::RefPtr<ThreadDispatcher> futex_owner);
    ~FutexNode();

    FutexNode(const FutexNode &) = delete;
    FutexNode& operator=(const FutexNode &) = delete;

    bool IsInQueue() const;
    void SetAsSingletonList();

    // adds a list of nodes to our tail
    void AppendList(FutexNode* head);

    static FutexNode* RemoveNodeFromList(FutexNode* list_head, FutexNode* node);

    static FutexNode* WakeThreads(FutexNode* node,
                                  uint32_t count,
                                  uintptr_t old_hash_key,
                                  OwnerAction owner_action);

    static FutexNode* RemoveFromHead(FutexNode* list_head,
                                     uint32_t count,
                                     uintptr_t old_hash_key,
                                     uintptr_t new_hash_key);

    // This must be called with a guard held in the calling scope. Releases the
    // guard and does not reacquire it.
    zx_status_t BlockThread(Guard<fbl::Mutex>&& adopt_guard, const Deadline& deadline);

    void set_hash_key(uintptr_t key) {
        hash_key_ = key;

        // TODO(johngro): restructure so that we can assert that we are holding
        // the appropriate futex context lock before doing this.
        waiting_thread_->blocking_futex_id() = key;
    }

    // Used for debug asserts only
    uintptr_t waiting_thread_hash_key() const { return waiting_thread_->blocking_futex_id(); }

    // Trait implementation for fbl::HashTable
    uintptr_t GetKey() const { return hash_key_; }
    static size_t GetHash(uintptr_t key) { return (key >> 3); }

    fbl::RefPtr<ThreadDispatcher>& futex_owner() { return futex_owner_; }

private:
    static void RelinkAsAdjacent(FutexNode* node1, FutexNode* node2);
    static void SpliceNodes(FutexNode* node1, FutexNode* node2);

    void WakeThread();

    void MarkAsNotInQueue();

    // hash_key_ contains the futex address.  This field has two roles:
    //  * It is used by FutexWait() to determine which queue to remove the
    //    thread from when a wait operation times out.
    //  * Additionally, when this FutexNode is the head of a futex wait
    //    queue, this field is used by the HashTable (because it uses
    //    intrusive SinglyLinkedLists).
    uintptr_t hash_key_;

    // futex_owner_ holds a reference to the thread who is currently considered
    // to be the "owner" of the futex for priority inheritance purposes.  Only
    // the head of a list of waiters holds a reference to the owner at any point
    // in time.  When threads leave the list of owners, if the thread leaving is
    // the head of the list, it is important that it properly transfer ownership
    // depending on the situation.  Specifically...
    //
    // ++ When any number of threads are removed from the list as part of a wake
    //    operation with the OwnerAction::RELEASE behavior set, the new owner of
    //    the futex will be nullptr.
    // ++ When a single thread is removed from the list as part of a wake
    //    operation with the OwnerAction::ASSIGN_WOKEN behavior set, the new owner of
    //    the futex becomes the thread which was woken.
    // ++ When a thread times out during a futex wait operation, the ownership
    //    state of the futex is preserved.  Specifically, if the thread who
    //    timed out had been the head of the list, then the futex_owner_ field
    //    must be transfered to the new head of the list, if any.
    // ++ When one or more threads are requeued to wait on a different futex,
    //    the ownership state of the futex is preserved.  Specifically, if any
    //    of the threads who are being requeued had been the head of the list,
    //    then the futex_owner_ field must be transfered to the new head of the
    //    list, if any.
    fbl::RefPtr<ThreadDispatcher> futex_owner_;

    // waiting_thread_ holds a reference to the thread dispatcher whose
    // FutexNode this is.  It is used during thread wakeup situations in order
    // to transfer ownership of the futex to the thread which was woken up.
    //
    // In theory, we should be able to grab this reference from the wait_queue_
    // member (below), but then we would need a way to deal with a possible, but
    // rare, race.  It goes like this.
    //
    // 1) Thread A is waiting on futex X with a timeout.
    // 2) Thread B performs a wake operation.  It enters the futex lock and is
    //    about to enter the global thread lock and call WakeOne on the
    //    wait_queue_ at the head of the futex wait queue.
    // 3) Before it does, thread A times out leaving the wait_queue_ empty.
    // 4) Thread B makes it into the global thread lock, but the wait_queue_ is
    //    now empty, so it has no way to assign ownership of the futex to thread
    //    A.
    fbl::RefPtr<ThreadDispatcher> waiting_thread_;

    // Used for waking the thread corresponding to the FutexNode.
    WaitQueue wait_queue_;

    // queue_prev_ and queue_next_ are used for maintaining a circular
    // doubly-linked list of threads that are waiting on one futex address.
    //  * When the list contains only this node, queue_prev_ and
    //    queue_next_ both point back to this node.
    //  * When the thread is not waiting on a futex, queue_next_ is null.
    FutexNode* queue_prev_ = nullptr;
    FutexNode* queue_next_ = nullptr;
};
