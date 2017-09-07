// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/futex_node.h>

#include <assert.h>
#include <err.h>
#include <fbl/mutex.h>
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

    wait_queue_destroy(&wait_queue_);
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

// This removes up to |count| threads from the list specified by |node|,
// and it wakes those threads.  It returns the new list head (i.e. the list
// of remaining nodes), which may be null (empty).
//
// This will always remove at least one node, because it requires that
// |count| is non-zero and |list_head| is a non-empty list.
//
// RemoveFromHead() is similar, except that it produces a list of removed
// threads without waking them.
FutexNode* FutexNode::WakeThreads(FutexNode* node, uint32_t count,
                                  uintptr_t old_hash_key, bool* out_any_woken) {
    ASSERT(node);
    ASSERT(count != 0);

    FutexNode* const list_end = node->queue_prev_;
    for (uint32_t i = 0; i < count; i++) {
        DEBUG_ASSERT(node->GetKey() == old_hash_key);
        // Clear this field to avoid any possible confusion.
        node->set_hash_key(0);

        const bool is_last_node = (node == list_end);
        FutexNode* next = node->queue_next_;
        // This call can cause |node| to be freed, so we must not
        // dereference |node| after this.
        if (node->WakeThread())
            *out_any_woken = true;

        if (is_last_node) {
            // We have reached the end of the list, so we are removing all
            // the entries from the list.  Return an empty list of
            // remaining nodes.
            return nullptr;
        }
        node = next;
    }

    // Restore the list invariant for the list of remaining waiter nodes.
    RelinkAsAdjacent(list_end, node);
    return node;
}

// This removes up to |count| nodes from |list_head|.  It returns the new
// list head (i.e. the list of remaining nodes), which may be null (empty).
// On return, |list_head| is the list of nodes that were removed --
// |list_head| remains a valid list.
//
// This will always remove at least one node, because it requires that
// |count| is non-zero and |list_head| is a non-empty list.
//
// WakeThreads() is similar, except that it wakes the threads that it
// removes from the list.
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
mx_status_t FutexNode::BlockThread(fbl::Mutex* mutex, mx_time_t deadline) TA_NO_THREAD_SAFETY_ANALYSIS {
    AutoThreadLock lock;

    // We specifically want reschedule=false here, otherwise the
    // combination of releasing the mutex and enqueuing the current thread
    // would not be atomic, which would mean that we could miss wakeups.
    mutex_release_thread_locked(mutex->GetInternal(), /* reschedule= */ false);

    thread_t* current_thread = get_current_thread();
    mx_status_t result;
    current_thread->interruptable = true;
    result = wait_queue_block(&wait_queue_, deadline);
    current_thread->interruptable = false;

    return result;
}

// This returns whether the thread was woken.  This will usually return
// true, but can sometimes return false if our wakeup coincides with the
// thread waking up from a timeout.
bool FutexNode::WakeThread() {
    // We must be careful to correctly handle the case where the thread
    // for |this| wakes and exits, deleting |this|.  There are two
    // cases to consider:
    //  1) The thread's wait times out, or the thread is killed or
    //     suspended.  In those cases, FutexWait() will reacquire the
    //     FutexContext lock.  We are currently holding that lock, so
    //     FutexWait() will not race with us.
    //  2) The thread is woken by our wait_queue_wake_one() call.  In
    //     this case, FutexWait() will *not* reacquire the FutexContext
    //     lock.  To handle this correctly, we must not access |this|
    //     after wait_queue_wake_one().

    // We must do this before we wake the thread, to handle case 2.
    MarkAsNotInQueue();

    // Place the waiting thread in the runnable state, but do not
    // reschedule yet.  Our caller is currently holding the main
    // futex_lock, and any threads which get woken by this action are going
    // to immediately attempt to obtain the main futex_lock.  If we
    // indicate that the thread was woken during this process, our caller
    // will release the lock and then arrange for a reschedule operation
    // (which leads to a smoother transition).
    AutoThreadLock lock;
    return wait_queue_wake_one(&wait_queue_, /* reschedule */ false, MX_OK);
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
