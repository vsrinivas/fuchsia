// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zircon-internal/macros.h>

#include <kernel/thread.h>
#include <ktl/limits.h>
#include <object/thread_dispatcher.h>
#include <vm/page.h>
#include <vm/stack_owned_loaned_pages_interval.h>

#include <ktl/enforce.h>

void StackOwnedLoanedPagesInterval::PrepareForWaiter() {
  canary_.Assert();
  // For now we don't need a CAS loop in here because thread_lock is held by callers of
  // PrepareForWaiter() and PrepareForWaiter() is the only mutator of is_ready_for_waiter_.  Even if
  // we did have a CAS loop, the caller would still need to guarantee somehow that the interval
  // won't get deleted out from under this call.  Currently that's guaranteed by the current
  // thread_lock hold interval being the same interval that set kObjectOrStackOwnerHasWaiter.
  //
  // Because all setters of is_ready_for_waiter_ hold thread_lock, we could use memory_order_relaxed
  // here, but for now we're using acquire for all loads of is_ready_for_waiter_.
  if (is_ready_for_waiter_.load(ktl::memory_order_acquire)) {
    return;
  }
  // Thanks to thread_lock, we know that the current thread is the only thread setting
  // is_ready_for_waiter_, so we can just set it using a store().  We also need to prepare the
  // owned_wait_queue_ to have a waiter that can transmit its priority via priority inheritance to
  // the stack-owning thread.
  DEBUG_ASSERT(owning_thread_);
  DEBUG_ASSERT(Thread::Current::Get() != owning_thread_);
  owned_wait_queue_.emplace();
  // The memory_order_release isn't really needed here thanks to release of thread_lock by this
  // thread shortly and acquire of thread_lock by any thread removing the
  // StackOwnedLoanedPagesInterval from the page (before deleting the interval), but for now we're
  // using release for all stores to is_ready_for_waiter_.
  is_ready_for_waiter_.store(true, ktl::memory_order_release);
}

// static
StackOwnedLoanedPagesInterval& StackOwnedLoanedPagesInterval::current() {
  Thread* current_thread = Thread::Current::Get();
  // The caller should only call current() when the caller knows there must be a current interval,
  // and just needs to know which interval is the outer-most on this thread's stack.
  //
  // Stack ownership of a loaned page requires having a StackOwnedLoanedPagesInterval on the
  // caller's stack.
  DEBUG_ASSERT_MSG(current_thread->stack_owned_loaned_pages_interval(),
                   "StackOwnedLoanedPagesInterval missing");
  return *current_thread->stack_owned_loaned_pages_interval_;
}

// static
StackOwnedLoanedPagesInterval* StackOwnedLoanedPagesInterval::maybe_current() {
  Thread* current_thread = Thread::Current::Get();
  return current_thread->stack_owned_loaned_pages_interval_;
}

// static
void StackOwnedLoanedPagesInterval::WaitUntilContiguousPageNotStackOwned(vm_page_t* page) {
  // Due to not holding the PmmNode lock, we can't check loaned directly, and it may have been unset
  // recently in any case, but in that case we'll notice via !is_stack_owned() instead.
  //
  // Need to take thread_lock at this point, because avoiding deletion of the OwnedWaitQueue
  // requires holding the thread_lock while applying kObjectOrStackOwnerHasWaiter to the page, to
  // prevent the StackOwnedLoanedPagesInterval thread from removing the stack_owner from the page
  // and deleting the OwnedWaitQueue.  We also need the thread_lock to block on the
  // OwnedWaitQueue.
  //
  // Before we acquire the thread_lock we do a check whether a stack_owner is still set. This is
  // just to avoid acquiring the thread lock on the off chance that the stack ownership interval
  // is already over.  This isn't particularly likely to be the case, and we'd be fine without
  // this check.  But since we're about to take the thread_lock let's avoid an unnecessary acquire
  // if we can.
  if (!page->object.is_stack_owned()) {
    // StackOwnedLoanedPagesInterval is already removed from the page, so no need to
    // acquire the thread_lock.  Go around and observe the new page state.
    return;
  }
  // Acquire thread_lock since that's required to ensure ~StackOwnedLoanedPagesInterval doesn't
  // miss that this thread is blocked waiting, along with kObjectOrStackOwnerHasWaiter.
  AnnotatedAutoPreemptDisabler aapd;
  Guard<MonitoredSpinLock, IrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};
  // Holding the thread_lock doesn't guarantee that the stack_owner won't be cleared, but holding
  // thread_lock and successfully ensuring that kObjectOrStackOwnerHasWaiter is set does guarantee
  // the stack_owner won't be cleared.
  auto maybe_try_set_has_waiter_result = page->object.try_set_has_waiter();
  if (!maybe_try_set_has_waiter_result) {
    // stack_owner was cleared; no need to wait.
    //
    // ~thread_lock_guard
    return;
  }
  auto& try_set_has_waiter_result = maybe_try_set_has_waiter_result.value();
  auto& stack_owner = *try_set_has_waiter_result.stack_owner;
  // By doing PrepareForWaiter() only when necessary, we avoid pressure on the thread_lock in the
  // case where there's no page reclaiming thread needing to wait / transmit priority.
  if (try_set_has_waiter_result.first_setter) {
    stack_owner.PrepareForWaiter();
  }
  // PrepareForWaiter() was called previously, either by this thread or a different thread.
  DEBUG_ASSERT(stack_owner.is_ready_for_waiter_.load(ktl::memory_order_acquire));

  // At this point we know that the stack_owner won't be changed on the page while we hold
  // thread_lock, which means the OwnedWaitQueue can't be deleted yet either, since deletion is
  // after uninstalling from the page.  So now we just need to block on the OwnedWaitQueue, which
  // requires holding the thread_lock during the call anyway.  We don't really care if this
  // OwnedWaitQueue is relevant to moving from cow to cow, or cow to FREE, or during ALLOC state.
  // In all those possible cases, we want to block on the OwnedWaitQueue.  The fact that the
  // OwnedWaitQueue is there is reason enough to block on it, since we want to wait for the page
  // to be outside any stack ownership interval.
  //
  // If this is the first thread blocking in the OWQ, we expect the queue's owner to be nullptr.
  // For subsequent blocking threads, we expect it to match our owning_thread_.
  DEBUG_ASSERT((stack_owner.owned_wait_queue_->owner() == nullptr) ||
               (stack_owner.owned_wait_queue_->owner() == stack_owner.owning_thread_));
  DEBUG_ASSERT(stack_owner.owned_wait_queue_->owner() != Thread::Current::Get());

  // This is a brief wait that's guaranteed not to get stuck (short of bugs elsewhere), with
  // priority inheritance propagated to the owning thread.  So no need for a deadline or
  // interruptible.
  zx_status_t block_status = stack_owner.owned_wait_queue_->BlockAndAssignOwner(
      Deadline::infinite(), stack_owner.owning_thread_, ResourceOwnership::Normal,
      Interruptible::No);

  // For this wait queue, no other status is possible since no other status is ever passed to
  // OwnedWaitQueue::WakeAll() for this wait queue and Block() doesn't have any other sources of
  // failures assuming no bugs here.
  DEBUG_ASSERT(block_status == ZX_OK);
}

void StackOwnedLoanedPagesInterval::WakeWaitersAndClearOwner(Thread* current_thread) {
  DEBUG_ASSERT(current_thread == Thread::Current::Get());
  auto hook = [](Thread* woken, void* ctx) -> OwnedWaitQueue::Hook::Action {
    return OwnedWaitQueue::Hook::Action::SelectAndKeepGoing;
  };
  AnnotatedAutoPreemptDisabler aapd;
  Guard<MonitoredSpinLock, IrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};
  DEBUG_ASSERT(owned_wait_queue_->owner() == current_thread);

  // Release the ownership before waking all of the threads.  This is a minor
  // optimization; it causes all of the inherited profile values of the owner
  // thread to be updated all at once, instead of one woken thread at a time.
  //
  // We do not need to be concerned that we will become de-scheduled here as a
  // result of a loss of profile pressure, as we disabled preemption just a few
  // lines before this.  This is always a requirement when interacting with an
  // OwnedWaitQueue in any way which might cause the current thread to become a
  // less favorable choice than one of the threads its actions affected in the
  // PI graph.
  owned_wait_queue_->AssignOwner(nullptr);
  owned_wait_queue_->WakeThreads(ktl::numeric_limits<uint32_t>::max(), {hook, nullptr});
}
