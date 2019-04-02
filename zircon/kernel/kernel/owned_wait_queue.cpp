// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/mp.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/sched.h>
#include <kernel/wait_queue_internal.h>

namespace {

// Update our reschedule related kernel counters and send a request for any
// needed IPIs.
inline void UpdateStatsAndSendIPIs(bool local_resched, cpu_mask_t accum_cpu_mask)
    TA_REQ(thread_lock) {
    // TODO(johngro): Implement this when we actually implement PI propagation
}

}  // anon namespace

OwnedWaitQueue::~OwnedWaitQueue() {
    // Something is very very wrong if we have been allowed to destruct while we
    // still have an owner.
    DEBUG_ASSERT(owner_ == nullptr);
}

void OwnedWaitQueue::DisownAllQueues(thread_t* t) {
    // It is important that this thread not be blocked by any other wait queues
    // during this operation.  If it was possible for the thread to be blocked,
    // we would need to update all of the PI chain bookkeeping too.
    DEBUG_ASSERT(t->blocking_wait_queue == nullptr);

    for (auto& q : t->owned_wait_queues) {
        DEBUG_ASSERT(q.owner_ == t);
        q.owner_ = nullptr;
    }

    t->owned_wait_queues.clear();
}

bool OwnedWaitQueue::QueuePressureChanged(thread_t* t,
                                          int old_prio,
                                          int new_prio,
                                          cpu_mask_t* accum_cpu_mask) TA_REQ(thread_lock) {
    // TODO(johngro): Implement this when we actually implement PI propagation
    return false;
}

bool OwnedWaitQueue::WaitersPriorityChanged(int old_prio) {
    if (owner() == nullptr) {
        return false;
    }

    int new_prio = wait_queue_blocked_priority(this);
    if (old_prio == new_prio) {
        return false;
    }

    cpu_mask_t accum_cpu_mask = 0;
    bool local_resched = QueuePressureChanged(owner(), old_prio, new_prio, &accum_cpu_mask);
    UpdateStatsAndSendIPIs(local_resched, accum_cpu_mask);
    return local_resched;
}

bool OwnedWaitQueue::UpdateBookkeeping(thread_t* new_owner,
                                       int old_prio,
                                       cpu_mask_t* out_accum_cpu_mask) {
    int new_prio = wait_queue_blocked_priority(this);
    bool local_resched = false;
    cpu_mask_t accum_cpu_mask = 0;

    // The new owner may not be a dying thread.
    if ((new_owner != nullptr) && (new_owner->state == THREAD_DEATH)) {
        new_owner = nullptr;
    }

    if (new_owner == owner()) {
        // The owner has not changed.  If there never was an owner, or there is
        // an owner but the queue pressure has not changed, then there is
        // nothing we need to do.
        if ((owner() == nullptr) || (new_prio == old_prio)) {
            return false;
        }

        local_resched = QueuePressureChanged(owner(), old_prio, new_prio, &accum_cpu_mask);
    } else {
        // Looks like the ownership has actually changed.  Start releasing
        // ownership and propagating the PI consequences for the old owner (if
        // any).
        thread_t* old_owner = owner();
        if (old_owner != nullptr) {
            DEBUG_ASSERT(this->InContainer());
            old_owner->owned_wait_queues.erase(*this);

            if ((old_prio >= 0) && QueuePressureChanged(old_owner, old_prio, -1, &accum_cpu_mask)) {
                local_resched = true;
            }
        }

        // Update to the new owner.  If there is a new owner, fix the
        // bookkeeping.  Then, if there are waiters in the queue (therefore,
        // non-negative pressure), then apply that pressure now.
        owner_ = new_owner;
        if (new_owner != nullptr) {
            DEBUG_ASSERT(!this->InContainer());
            new_owner->owned_wait_queues.push_back(this);

            if ((new_prio >= 0) && QueuePressureChanged(new_owner, -1, new_prio, &accum_cpu_mask)) {
                local_resched = true;
            }
        }
    }

    if (out_accum_cpu_mask != nullptr) {
        *out_accum_cpu_mask |= accum_cpu_mask;
    } else {
        UpdateStatsAndSendIPIs(local_resched, accum_cpu_mask);
    }

    return local_resched;
}

bool OwnedWaitQueue::WakeThreadsInternal(uint32_t wake_count,
                                         thread_t** out_new_owner,
                                         Hook on_thread_wake_hook) {
    DEBUG_ASSERT(magic == MAGIC);
    DEBUG_ASSERT(out_new_owner != nullptr);

    // Note: This methods relies on the wait queue to be kept sorted in the
    // order that the scheduler would prefer to wake threads.
    *out_new_owner = nullptr;

    // If our wake_count is zero, then there is nothing to do.
    if (wake_count == 0) {
        return false;
    }

    bool do_resched = false;
    uint32_t woken = 0;
    ForeachThread([&](thread_t* t) TA_REQ(thread_lock) -> bool {
        // Call the user supplied hook and let them decide what to do with this
        // thread (updating their own bookkeeping in the process)
        using Action = Hook::Action;
        Action action = on_thread_wake_hook(t);

        // If the user wants to skip this thread, just move on to the next.
        if (action == Action::Skip) {
            return true;
        }

        // All other choices involve waking up this thread, so go ahead and do that now.
        ++woken;
        DequeueThread(t, ZX_OK);
        if (sched_unblock(t)) {
            do_resched = true;
        }

        // If we are supposed to keep going, do so now if we are still permitted
        // to wake more threads.
        if (action == Action::SelectAndKeepGoing) {
            return (woken < wake_count);
        }

        // No matter what the user chose at this point, we are going to stop
        // after this.  If the user requested that we assign ownership to the
        // thread we just woke, make sure that we have not woken any other
        // threads, and return a pointer to the thread who is to become the new
        // owner if there are still threads waiting in the queue.
        if (action == Action::SelectAndAssignOwner) {
            DEBUG_ASSERT(woken == 1);
            if (!IsEmpty()) {
                *out_new_owner = t;
            }
        } else {
            // SelectAndStop is the only remaining valid option.
            DEBUG_ASSERT(action == Action::SelectAndStop);
        }

        return false;
    });

    return do_resched;
}

zx_status_t OwnedWaitQueue::BlockAndAssignOwner(const Deadline& deadline,
                                                thread_t* new_owner,
                                                ResourceOwnership resource_ownership) {
    thread_t* current_thread = get_current_thread();

    DEBUG_ASSERT(magic == MAGIC);
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    // Remember what the maximum effective priority of the wait queue was before
    // we add current_thread to it.
    int old_queue_prio = wait_queue_blocked_priority(this);

    // Perform the first half of the block_etc operation.  If this fails, then
    // the state of the actual wait queue is unchanged and we can just get out
    // now.
    zx_status_t res = internal::wait_queue_block_etc_pre(this, deadline, 0u, resource_ownership);
    if (res != ZX_OK) {
        return res;
    }

    // Success.  The current thread has passed all of its sanity checks and been
    // added to the wait queue.  Go ahead and update our priority inheritance
    // bookkeeping since both ownership and current PI pressure may have changed
    // (ownership because of |new_owner| and pressure because of the addition of
    // the thread to the queue.
    //
    // Note: it is safe to ignore the local reschedule hint passed back from
    // UpdateBookkeeping.  We are just about to block this thread, which is
    // going to trigger a reschedule anyway.
    __UNUSED bool local_resched;
    local_resched = UpdateBookkeeping(new_owner, old_queue_prio);

    // Finally, go ahead and run the second half of the block_etc operation.
    // This will actually block our thread and handle setting any timeout timers
    // in the process.
    return internal::wait_queue_block_etc_post(this, deadline);
}

bool OwnedWaitQueue::WakeThreads(uint32_t wake_count, Hook on_thread_wake_hook) {
    DEBUG_ASSERT(magic == MAGIC);

    thread_t* new_owner;
    int old_queue_prio = wait_queue_blocked_priority(this);
    bool local_resched = WakeThreadsInternal(wake_count,
                                             &new_owner,
                                             on_thread_wake_hook);


    if (UpdateBookkeeping(new_owner, old_queue_prio)) {
        local_resched = true;
    }

    return local_resched;
}

bool OwnedWaitQueue::WakeAndRequeue(uint32_t wake_count,
                                    OwnedWaitQueue* requeue_target,
                                    uint32_t requeue_count,
                                    thread_t* requeue_owner,
                                    Hook on_thread_wake_hook,
                                    Hook on_thread_requeue_hook) {
    DEBUG_ASSERT(magic == MAGIC);
    DEBUG_ASSERT(requeue_target != nullptr);
    DEBUG_ASSERT(requeue_target->magic == MAGIC);

    // If the potential new owner of the requeue wait queue is already dead,
    // then it cannot become the owner of the requeue wait queue.
    if (requeue_owner != nullptr) {
        // It should not be possible for a thread which is not yet running to be
        // declared as the owner of an OwnedWaitQueue.
        DEBUG_ASSERT(requeue_owner->state != THREAD_INITIAL);
        if (requeue_owner->state == THREAD_DEATH) {
            requeue_owner = nullptr;
        }
    }

    // Remember what our queue priorities had been.  We will need this when it
    // comes time to update the PI chains.
    int old_wake_prio = wait_queue_blocked_priority(this);
    int old_requeue_prio = wait_queue_blocked_priority(requeue_target);

    thread_t* new_wake_owner;
    bool local_resched = WakeThreadsInternal(wake_count,
                                             &new_wake_owner,
                                             on_thread_wake_hook);

    // If there are still threads left in the wake queue (this), and we were asked to
    // requeue threads, then do so.
    if (!this->IsEmpty() && requeue_count) {
        uint32_t requeued = 0;
        ForeachThread([&](thread_t* t) TA_REQ(thread_lock) -> bool {
            // Call the user's requeue hook so that we can decide what to do
            // with this thread.
            using Action = Hook::Action;
            Action action = on_thread_requeue_hook(t);

            // It is illegal to ask for a requeue operation to assign ownership.
            DEBUG_ASSERT(action != Action::SelectAndAssignOwner);

            // If the user wants to skip this thread, just move on to the next.
            if (action == Action::Skip) {
                return true;
            }

            // Actually move the thread from this to the requeue_target.
            WaitQueue::MoveThread(this, requeue_target, t);
            ++requeued;

            // Are we done?
            if (action == Action::SelectAndStop) {
                return false;
            }

            // SelectAndKeepGoing is the only legal choice left.
            DEBUG_ASSERT(action == Action::SelectAndKeepGoing);
            return (requeued < requeue_count);
        });
    }

    // Now that we are finished moving everyone around, update the ownership of
    // the queues involved in the operation.  These updates should deal with
    // propagating any priority inheritance consequences of the requeue
    // operation.
    cpu_mask_t accum_cpu_mask = 0;
    bool pi_resched = false;

    if (UpdateBookkeeping(new_wake_owner, old_wake_prio, &accum_cpu_mask)) {
        pi_resched = true;
    }

    if (requeue_target->UpdateBookkeeping(requeue_owner, old_requeue_prio, &accum_cpu_mask)) {
        pi_resched = true;
    }

    UpdateStatsAndSendIPIs(pi_resched, accum_cpu_mask);
    return local_resched || pi_resched;
}
