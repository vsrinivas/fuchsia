// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/futex_context.h>

#include <assert.h>
#include <lib/user_copy/user_ptr.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

namespace {     // file scope only
inline zx_status_t ValidateFutexPointer(user_in_ptr<const zx_futex_t> value_ptr) {
    if (!value_ptr || (reinterpret_cast<uintptr_t>(value_ptr.get()) % sizeof(zx_futex_t))) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

inline zx_status_t ValidateNewFutexOwner(zx_handle_t new_owner_handle,
                                         fbl::RefPtr<ThreadDispatcher>* new_owner_thread_out) {
    DEBUG_ASSERT(new_owner_thread_out != nullptr);
    DEBUG_ASSERT(*new_owner_thread_out == nullptr);

    if (new_owner_handle == ZX_HANDLE_INVALID) {
        return ZX_OK;
    }

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t status = up->GetDispatcherWithRights(new_owner_handle, 0, new_owner_thread_out);
    if (status != ZX_OK) {
        return status;
    }

    // The thread has to be a member of the calling process.  Futexes may not be
    // owned by threads from another process.
    if ((*new_owner_thread_out)->process() != up) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}
}  // anon namespace

FutexContext::FutexContext() {
    LTRACE_ENTRY;
}

FutexContext::~FutexContext() {
    LTRACE_ENTRY;

    // All of the threads should have removed themselves from wait queues
    // by the time the process has exited.
    DEBUG_ASSERT(futex_table_.is_empty());
}

zx_status_t FutexContext::FutexWait(user_in_ptr<const zx_futex_t> value_ptr,
                                    zx_futex_t current_value, zx_handle_t new_futex_owner,
                                    const Deadline& deadline) {
    LTRACE_ENTRY;
    zx_status_t result;

    // Make sure the futex pointer is following the basic rules.
    result = ValidateFutexPointer(value_ptr);
    if (result != ZX_OK) {
        return result;
    }

    // Fetch a reference to the thread that the user is asserting is the new
    // futex owner, if any.
    fbl::RefPtr<ThreadDispatcher> futex_owner_thread;
    result = ValidateNewFutexOwner(new_futex_owner, &futex_owner_thread);
    if (result != ZX_OK) {
        return result;
    }

    // When attempting to wait, the new owner of the futex (if any) may not be
    // the thread which is attempting to wait.
    if (futex_owner_thread.get() == ThreadDispatcher::GetCurrent()) {
        return ZX_ERR_INVALID_ARGS;
    }

    // FutexWait() checks that the address value_ptr still contains
    // current_value, and if so it sleeps awaiting a FutexWake() on value_ptr.
    // Those two steps must together be atomic with respect to FutexWake().
    // If a FutexWake() operation could occur between them, a userland mutex
    // operation built on top of futexes would have a race condition that
    // could miss wakeups.
    Guard<fbl::Mutex> guard{&lock_};

    int value;
    result = value_ptr.copy_from_user(&value);
    if (result != ZX_OK) {
        return result;
    }
    if (value != current_value) {
        return ZX_ERR_BAD_STATE;
    }

    // Verify that the thread we are attempting to make the requeue target's
    // owner (if any) is not already waiting on the target futex.
    //
    // !! NOTE !!
    // This check *must* be done inside of the futex contex lock.  Right now,
    // there is not a great way to enforce this using clang's static thread
    // analysis.
    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr.get());
    if ((futex_owner_thread != nullptr) && (futex_owner_thread->blocking_futex_id() == futex_key)) {
        return ZX_ERR_INVALID_ARGS;
    }

    FutexNode node(ktl::move(futex_owner_thread));
    node.set_hash_key(futex_key);
    node.SetAsSingletonList();
    QueueNodesLocked(&node);

    // Block current thread.  This releases lock_ and does not reacquire it.
    result = node.BlockThread(guard.take(), deadline);
    if (result == ZX_OK) {
        DEBUG_ASSERT(!node.IsInQueue());

        // If we were the last waiter during the wake operation, it is possible
        // that the futex_owner reference was left in our stack based node
        // structure.  Go ahead and explictly release it now since we are not in
        // the per-process futex lock.
        node.futex_owner().reset();

        // All the work necessary for removing us from the hash table was done
        // by FutexWake()
        return ZX_OK;
    }

    // The following happens if we hit the deadline (ZX_ERR_TIMED_OUT) or if
    // the thread was killed (ZX_ERR_INTERNAL_INTR_KILLED) or suspended
    // (ZX_ERR_INTERNAL_INTR_RETRY).
    //
    // We need to ensure that the thread's node is removed from the wait
    // queue, because FutexWake() probably didn't do that.
    Guard<fbl::Mutex> guard2{&lock_};
    bool was_in_list = UnqueueNodeLocked(&node);

    // At this point, whether we were still in the queue or not after
    // entering the lock, we are no longer.  Explictly transfer any
    // futex_owner ThreadDispatcher reference into a stack local variable
    // outside of the mutex scope in order to ensure that we release any
    // ThreadDispatcher references from outside of the futex lock.
    futex_owner_thread = ktl::move(node.futex_owner());
    if (was_in_list) {
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
    return ZX_OK;
}

zx_status_t FutexContext::FutexWake(user_in_ptr<const zx_futex_t> value_ptr,
                                    uint32_t wake_count,
                                    FutexNode::OwnerAction owner_action) {
    LTRACE_ENTRY;
    zx_status_t result;

    // Make sure the futex pointer is following the basic rules.
    result = ValidateFutexPointer(value_ptr);
    if (result != ZX_OK) {
        return result;
    }

    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr.get());
    fbl::RefPtr<ThreadDispatcher> previous_owner;
    AutoReschedDisable resched_disable; // Must come before the Guard.
    resched_disable.Disable();
    {   // explicit lock scope for clarity.
        Guard<fbl::Mutex> guard{&lock_};
        FutexNode* node;

        // If we don't actually have any threads to wake, simply release futex
        // ownership if there are any threads currently blocked on the futex.
        if (wake_count == 0) {
            node = FindFutexQueue(futex_key);
            if (node != nullptr) {
                previous_owner = ktl::move(node->futex_owner());
            }
            return ZX_OK;
        }

        node = futex_table_.erase(futex_key);
        if (!node) {
            // nothing blocked on this futex if we can't find it
            return ZX_OK;
        }
        DEBUG_ASSERT(node->GetKey() == futex_key);

        // Before waking up any threads, move any pre-existing futex owner
        // reference into a scope where it will be released after we exit the
        // futex lock.  When we are done with the wake operation, the new futex
        // owner will be either nothing, or it will be the thread which we just
        // woke up, but it is not going to be the previous owner.
        previous_owner = ktl::move(node->futex_owner());

        FutexNode* remaining_waiters =
            FutexNode::WakeThreads(node, wake_count, futex_key, owner_action);

        if (remaining_waiters) {
            DEBUG_ASSERT(remaining_waiters->GetKey() == futex_key);
            futex_table_.insert(remaining_waiters);
        }
    }

    return ZX_OK;
}

zx_status_t FutexContext::FutexRequeue(user_in_ptr<const zx_futex_t> wake_ptr,
                                       uint32_t wake_count,
                                       int current_value,
                                       FutexNode::OwnerAction owner_action,
                                       user_in_ptr<const zx_futex_t> requeue_ptr,
                                       uint32_t requeue_count,
                                       zx_handle_t new_requeue_owner) {
    LTRACE_ENTRY;
    zx_status_t result;

    // Make sure the futex pointers are following the basic rules.
    result = ValidateFutexPointer(wake_ptr);
    if (result != ZX_OK) {
        return result;
    }

    result = ValidateFutexPointer(requeue_ptr);
    if (result != ZX_OK) {
        return result;
    }

    if (wake_ptr.get() == requeue_ptr.get()) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Fetch a reference to the thread that the user is asserting is the new
    // requeue futex owner, if any.
    fbl::RefPtr<ThreadDispatcher> requeue_owner_thread;
    result = ValidateNewFutexOwner(new_requeue_owner, &requeue_owner_thread);
    if (result != ZX_OK) {
        return result;
    }

    fbl::RefPtr<ThreadDispatcher> previous_owner;
    AutoReschedDisable resched_disable; // Must come before the Guard.
    Guard<fbl::Mutex> guard{&lock_};

    int value;
    result = wake_ptr.copy_from_user(&value);
    if (result != ZX_OK) return result;
    if (value != current_value) return ZX_ERR_BAD_STATE;

    // Verify that the thread we are attempting to make the requeue target's
    // owner (if any) is not waiting on either the wake futex or the requeue
    // futex.
    //
    // !! NOTE !!
    // This check *must* be done inside of the futex contex lock.  Right now,
    // there is not a great way to enforce this using clang's static thread
    // analysis.
    uintptr_t wake_key = reinterpret_cast<uintptr_t>(wake_ptr.get());
    uintptr_t requeue_key = reinterpret_cast<uintptr_t>(requeue_ptr.get());
    if ((requeue_owner_thread != nullptr) &&
        ((requeue_owner_thread->blocking_futex_id() == wake_key) ||
         (requeue_owner_thread->blocking_futex_id() == requeue_key))) {
        return ZX_ERR_INVALID_ARGS;
    }

    // This must happen before RemoveFromHead() calls set_hash_key() on
    // nodes below, because operations on futex_table_ look at the GetKey
    // field of the list head nodes for wake_key and requeue_key.
    FutexNode* wake_queue = futex_table_.erase(wake_key);
    if (!wake_queue) {
        // nothing blocked on this futex if we can't find it.  Make sure we
        // update the requeue owner if needed.
        FutexNode* requeue_queue = FindFutexQueue(requeue_key);
        if (requeue_queue != nullptr) {
            requeue_queue->futex_owner().swap(requeue_owner_thread);
        }

        return ZX_OK;
    }

    // This must come before WakeThreads() to be useful, but we want to
    // avoid doing it before copy_from_user() in case that faults.
    resched_disable.Disable();

    // Before waking up any threads, move any pre-existing futex owner
    // reference into a scope where it will be released after we exit the
    // futex lock.  When we are done with the wake operation, the new futex
    // owner will be either nothing, or it will be the thread which we just
    // woke up, but it is not going to be the previous owner.
    previous_owner = ktl::move(wake_queue->futex_owner());

    // Now, wake up some threads if we were asked to do so.
    if (wake_count > 0) {
        wake_queue = FutexNode::WakeThreads(wake_queue, wake_count, wake_key, owner_action);
    }

    // wake_queue is now the head of wake_ptr futex after possibly removing
    // some threads to wake
    if (wake_queue != nullptr) {
        if (requeue_count > 0) {
            // head and tail of list of wake_queues to requeue
            FutexNode* requeue_nodes = wake_queue;
            wake_queue = FutexNode::RemoveFromHead(wake_queue, requeue_count,
                                                   wake_key, requeue_key);

            // If we just removed _all_ of the remaining wake_queues from
            // the wake futex wait queue, and there had been a futex owner,
            // then requeue_head is still holding that owner reference.
            // Additionally, we want to explicitly set the requeue futex's
            // to the owner indicated by the caller.  Finally, we would
            // rather not release any ThreadDispatcher reference while we
            // are still inside of the master futex lock.
            //
            // Swap the requeue_head futex owner with the
            // requeue_owner_thread local variable.  This should cause the
            // requeue futex owner to be updated properly, and it should put
            // any old reference in the proper scope to be released after we
            // leave the futex lock.
            requeue_owner_thread.swap(requeue_nodes->futex_owner());

            // now requeue our requeue nodes to requeue_ptr mutex
            DEBUG_ASSERT(requeue_nodes->GetKey() == requeue_key);
            QueueNodesLocked(requeue_nodes);
        }
    }

    // add any remaining wake_queues back to wake_key futex
    if (wake_queue != nullptr) {
        DEBUG_ASSERT(wake_queue->GetKey() == wake_key);
        futex_table_.insert(wake_queue);
    }

    return ZX_OK;
}

zx_status_t FutexContext::FutexGetOwner(user_in_ptr<const zx_futex_t> value_ptr,
                                        user_out_ptr<zx_koid_t> koid_out) {
    zx_status_t result;

    // Make sure the futex pointer is following the basic rules.
    result = ValidateFutexPointer(value_ptr);
    if (result != ZX_OK) {
        return result;
    }

    zx_koid_t koid = ZX_KOID_INVALID;
    uintptr_t futex_key = reinterpret_cast<uintptr_t>(value_ptr.get());
    {
        Guard<fbl::Mutex> guard{&lock_};
        FutexNode* futex_queue = FindFutexQueue(futex_key);
        if ((futex_queue != nullptr) && (futex_queue->futex_owner() != nullptr)) {
            koid = futex_queue->futex_owner()->get_koid();
        }
    }

    return koid_out.copy_to_user(koid);
}

void FutexContext::QueueNodesLocked(FutexNode* head) {
    DEBUG_ASSERT(lock_.lock().IsHeld());

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
    DEBUG_ASSERT(lock_.lock().IsHeld());

    if (!node->IsInQueue()) {
        DEBUG_ASSERT(node->waiting_thread_hash_key() == 0u);
        return false;
    }

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
