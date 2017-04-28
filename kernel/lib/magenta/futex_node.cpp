// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <magenta/futex_node.h>
#include <magenta/magenta.h>
#include <platform.h>
#include <trace.h>

#define LOCAL_TRACE 0

FutexNode::FutexNode() {
    LTRACE_ENTRY;

    wait_queue_ = WAIT_QUEUE_INITIAL_VALUE(wait_queue_);
}

FutexNode::~FutexNode() {
    LTRACE_ENTRY;

    DEBUG_ASSERT(!IsInQueue());

    THREAD_LOCK(state);
    wait_queue_destroy(&wait_queue_);
    THREAD_UNLOCK(state);
}

bool FutexNode::IsInQueue() const {
    DEBUG_ASSERT((queue_next_ == nullptr) == (queue_prev_ == nullptr));
    return queue_next_ != nullptr;
}

void FutexNode::SetAsSingletonList() {
    DEBUG_ASSERT(!IsInQueue());
    queue_prev_ = this;
    queue_next_ = this;
}

void FutexNode::AppendList(FutexNode* head) {
    SpliceNodes(this, head);
}

// This removes |node| from the list whose first node is |list_head|.  This
// returns the new list head, or nullptr if the list has become empty.
FutexNode* FutexNode::RemoveNodeFromList(FutexNode* list_head,
                                         FutexNode* node) {
    if (node->queue_next_ == node) {
        DEBUG_ASSERT(node->queue_prev_ == node);
        // This list is a singleton, so after removing the node, the list
        // becomes empty.
        list_head = nullptr;
    } else {
        if (node == list_head) {
            // This node is the list head, so adjust the list head to be
            // the next node.
            list_head = node->queue_next_;
        }

        // Remove the node from the list.
        node->queue_next_->queue_prev_ = node->queue_prev_;
        node->queue_prev_->queue_next_ = node->queue_next_;
    }
    node->MarkAsNotInQueue();
    return list_head;
}

// This removes up to |count| nodes from |list_head|.  It returns the new
// list head (i.e. the list of remaining nodes), which may be null (empty).
// On return, |list_head| is the list of nodes that were removed --
// |list_head| remains a valid list.
//
// This will always remove at least one node, because it requires that
// |count| is non-zero and |list_head| is a non-empty list.
FutexNode* FutexNode::RemoveFromHead(FutexNode* list_head, uint32_t count,
                                     uintptr_t old_hash_key,
                                     uintptr_t new_hash_key) {
    ASSERT(list_head);
    ASSERT(count != 0);

    FutexNode* node = list_head;
    for (uint32_t i = 0; i < count; i++) {
        DEBUG_ASSERT(node->GetKey() == old_hash_key);
        // For requeuing, update the key so that FutexWait() can remove the
        // thread from its current queue if the wait operation times out.
        node->set_hash_key(new_hash_key);

        node = node->queue_next_;
        if (node == list_head) {
            // We have reached the end of the list, so we are removing all
            // the entries from the list.  Return an empty list of
            // remaining nodes.
            return nullptr;
        }
    }

    // Split the list into two lists.
    SpliceNodes(list_head, node);
    return node;
}

// This blocks the current thread.  This releases the given mutex (which
// must be held when BlockThread() is called).  To reduce contention, it
// does not reclaim the mutex on return.
status_t FutexNode::BlockThread(Mutex* mutex, mx_time_t deadline) TA_NO_THREAD_SAFETY_ANALYSIS {
    THREAD_LOCK(state);

    // We specifically want reschedule=false here, otherwise the
    // combination of releasing the mutex and enqueuing the current thread
    // would not be atomic, which would mean that we could miss wakeups.
    mutex_release_thread_locked(mutex->GetInternal(), /* reschedule= */ false);

    // Check whether a kill has been initiated, and block if not.  This
    // check+wait must be done atomically (with respect to THREAD_LOCK),
    // otherwise we could miss a thread termination.
    thread_t* current_thread = get_current_thread();
    status_t result;
    current_thread->interruptable = true;
    result = wait_queue_block(&wait_queue_, deadline);
    current_thread->interruptable = false;

    THREAD_UNLOCK(state);

    return result;
}

void FutexNode::WakeThreads(FutexNode* head) {
    if (!head)
        return;
    FutexNode* node = head;
    do {
        FutexNode* next = node->queue_next_;
        THREAD_LOCK(state);
        wait_queue_wake_one(&node->wait_queue_, true, NO_ERROR);
        THREAD_UNLOCK(state);
        node->MarkAsNotInQueue();
        node = next;
    } while (node != head);
}

// Set |node1| and |node2|'s list pointers so that |node1| is immediately
// before |node2| in the linked list.
void FutexNode::RelinkAsAdjacent(FutexNode* node1, FutexNode* node2) {
    node1->queue_next_ = node2;
    node2->queue_prev_ = node1;
}

// If |node1| and |node2| are separate lists, this combines them into one
// list.  If |node1| and |node2| are different nodes in the same list, this
// splits them into two separate lists.  (This operation happens to be a
// self-inverse.)
void FutexNode::SpliceNodes(FutexNode* node1, FutexNode* node2) {
    FutexNode* node1_prev = node1->queue_prev_;
    FutexNode* node2_prev = node2->queue_prev_;
    RelinkAsAdjacent(node1_prev, node2);
    RelinkAsAdjacent(node2_prev, node1);
}

void FutexNode::MarkAsNotInQueue() {
    queue_next_ = nullptr;
    // Unsetting queue_prev_ stops us from following an outdated pointer in
    // case we make a mistake with list manipulation.  Otherwise, it is
    // only required by the assertion in IsInQueue().
    queue_prev_ = nullptr;
}
