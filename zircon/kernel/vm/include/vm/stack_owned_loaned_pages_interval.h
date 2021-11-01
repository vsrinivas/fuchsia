// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_STACK_OWNED_LOANED_PAGES_INTERVAL_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_STACK_OWNED_LOANED_PAGES_INTERVAL_H_

#include <kernel/owned_wait_queue.h>
#include <kernel/thread.h>
#include <ktl/optional.h>

struct vm_page;

// This class establishes a RAII style code interval (while an instance of this class is on the
// stack).  During this interval, it is permissible to stack-own a loaned page.
//
// Intervals are allowed to nest.  The outermost interval (technically: first constructed) is the
// interval that applies.
//
// A thread that wants to wait for a loaned page to no longer be stack-owned can call
// WaitUntilContiguousPageNotStackOwned().  The wait will participate in priority inheritance which
// will boost the stack-owning thread to at least the priority of the waiting thread for the
// duration of the wait.
//
// At least for now, instances of this class are only meant to exist on the stack.  Heap allocation
// of an instance of this class is not currently supported, and will fail asserts if the destruction
// thread doesn't match the construction thread (and possibly other asserts).
class StackOwnedLoanedPagesInterval {
 public:
  // No copy or move.
  StackOwnedLoanedPagesInterval(const StackOwnedLoanedPagesInterval& to_copy) = delete;
  StackOwnedLoanedPagesInterval& operator=(const StackOwnedLoanedPagesInterval& to_copy) = delete;
  StackOwnedLoanedPagesInterval(StackOwnedLoanedPagesInterval&& to_move) = delete;
  StackOwnedLoanedPagesInterval& operator=(StackOwnedLoanedPagesInterval&& to_move) = delete;

  StackOwnedLoanedPagesInterval() {
    Thread* current_thread = Thread::Current::Get();
    // outermost interval wins; inner intervals don't do much
    if (unlikely(current_thread->stack_owned_loaned_pages_interval_)) {
      // Strictly speaking we don't need this assignment, but go ahead and set to nullptr in this
      // unlikely path, for the benefit of asserts in the destructor.
      owning_thread_ = nullptr;
      return;
    }
    // We delay AssignOnwer(current_thread) until PrepareForWaiter(), since often there will be no
    // waiter.
    owning_thread_ = current_thread;
    current_thread->stack_owned_loaned_pages_interval_ = this;
  }

  ~StackOwnedLoanedPagesInterval() {
    canary_.Assert();
    Thread* current_thread = Thread::Current::Get();
    // only remove if this is the outermost interval, which is likely
    if (likely(current_thread->stack_owned_loaned_pages_interval_ == this)) {
      DEBUG_ASSERT(owning_thread_);
      DEBUG_ASSERT(owning_thread_ == current_thread);
      current_thread->stack_owned_loaned_pages_interval_ = nullptr;
      if (unlikely(is_ready_for_waiter_.load(ktl::memory_order_acquire))) {
        // In the much more rare case that there are any waiters, wake all waiters and clear out the
        // owner before destructing owned_wait_queue_.  We do this out-of-line since it's not the
        // common path.
        WakeWaitersAndClearOwner(current_thread);
      }
      // PrepareForWaiter() was never called, so no need to acquire thread_lock.  This is very
      // likely.  Done.
    }
  }

  static StackOwnedLoanedPagesInterval& current();
  static StackOwnedLoanedPagesInterval* maybe_current();

  static void WaitUntilContiguousPageNotStackOwned(vm_page* page) TA_EXCL(thread_lock);

 private:
  // This sets up to permit a waiter, and asserts that the calling thread is not the constructing
  // thread, since waiting by the constructing/destructing thread would block (or maybe fail).
  void PrepareForWaiter() TA_REQ(thread_lock);

  void WakeWaitersAndClearOwner(Thread* current_thread) TA_EXCL(thread_lock);

  // magic value
  fbl::Canary<fbl::magic("SOPI")> canary_;
  // We stash the owning thread as part of delaying OwnedWaitQueue::AssignOwner(), to avoid putting
  // unnecessary pressure on thread_lock when there's no waiter.
  //
  // Only set during the constructor.  Only read after that.  Intentionally not initialized here to
  // avoid redundant initialization.
  Thread* owning_thread_;
  // This is atomic because in the common case of no waiter, the OwnedWaitQueue
  // isn't created and the
  // This is atomic because in the common case of no waiter, the thread with
  // StackOwnedLoanedPagesInterval on its stack doesn't need to acquire the thread_lock to read
  // false from here during destruction (and so never needs to acquire thread_lock).
  ktl::atomic<bool> is_ready_for_waiter_{false};
  // In the common case of no waiter, this never gets constructed or destructed.  We only construct
  // this on PrepareForWaiter() during WaitUntilContiguousPageNotStackOwned().
  ktl::optional<OwnedWaitQueue> owned_wait_queue_;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_STACK_OWNED_LOANED_PAGES_INTERVAL_H_
