// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/brwlock.h"

#include <kernel/thread_lock.h>
#include <ktl/limits.h>

namespace internal {

template <BrwLockEnablePi PI>
BrwLock<PI>::~BrwLock() {
  DEBUG_ASSERT(state_.state_.load(ktl::memory_order_relaxed) == 0);
}

template <BrwLockEnablePi PI>
void BrwLock<PI>::Block(bool write) {
  zx_status_t ret;

  if constexpr (PI == BrwLockEnablePi::Yes) {
    ret = wait_.BlockAndAssignOwner(Deadline::infinite(),
                                    state_.writer_.load(ktl::memory_order_relaxed),
                                    write ? ResourceOwnership::Normal : ResourceOwnership::Reader);
  } else {
    ret = write ? wait_.Block(Deadline::infinite()) : wait_.BlockReadLock(Deadline::infinite());
  }
  if (unlikely(ret < ZX_OK)) {
    panic(
        "BrwLock<%d>::Block: Block returned with error %d lock %p, thr %p, "
        "sp %p\n",
        static_cast<bool>(PI), ret, this, get_current_thread(), __GET_FRAME());
  }
}

template <BrwLockEnablePi PI>
ResourceOwnership BrwLock<PI>::Wake() {
  if constexpr (PI == BrwLockEnablePi::Yes) {
    using Action = OwnedWaitQueue::Hook::Action;
    struct Context {
      ResourceOwnership ownership;
      BrwLockState<PI>& state;
    };
    Context context = {ResourceOwnership::Normal, state_};
    auto cbk = [](thread_t* woken, void* ctx) -> Action {
      Context* context = reinterpret_cast<Context*>(ctx);

      if (context->ownership == ResourceOwnership::Normal) {
        // Check if target is blocked for writing and not reading
        if (woken->state == THREAD_BLOCKED) {
          context->state.writer_.store(woken, ktl::memory_order_relaxed);
          context->state.state_.fetch_add(-kBrwLockWaiter + kBrwLockWriter,
                                          ktl::memory_order_acq_rel);
          return Action::SelectAndAssignOwner;
        }
        // If not writing then we must be blocked for reading
        DEBUG_ASSERT(woken->state == THREAD_BLOCKED_READ_LOCK);
        context->ownership = ResourceOwnership::Reader;
      }
      // Our current ownership is ResourceOwnership::Reader otherwise we would
      // have returned early
      DEBUG_ASSERT(context->ownership == ResourceOwnership::Reader);
      if (woken->state == THREAD_BLOCKED_READ_LOCK) {
        // We are waking readers and we found a reader, so we can wake them up and
        // search for me.
        context->state.state_.fetch_add(-kBrwLockWaiter + kBrwLockReader,
                                        ktl::memory_order_acq_rel);
        return Action::SelectAndKeepGoing;
      } else {
        // We are waking readers but we have found a writer. To preserve fairness we
        // immediately stop and do not wake this thread or any others.
        return Action::Stop;
      }
    };

    bool resched = wait_.WakeThreads(ktl::numeric_limits<uint32_t>::max(), {cbk, &context});
    if (resched) {
      sched_reschedule();
    }
    return context.ownership;
  } else {
    thread_t* next = wait_.Peek();
    DEBUG_ASSERT(next != NULL);
    if (next->state == THREAD_BLOCKED_READ_LOCK) {
      while (!wait_.IsEmpty()) {
        thread_t* next = wait_.Peek();
        if (next->state != THREAD_BLOCKED_READ_LOCK) {
          break;
        }
        state_.state_.fetch_add(-kBrwLockWaiter + kBrwLockReader, ktl::memory_order_acq_rel);
        wait_.UnblockThread(next, ZX_OK);
      }
      return ResourceOwnership::Reader;
    } else {
      state_.state_.fetch_add(-kBrwLockWaiter + kBrwLockWriter, ktl::memory_order_acq_rel);
      wait_.UnblockThread(next, ZX_OK);
      return ResourceOwnership::Normal;
    }
  }
}

template <BrwLockEnablePi PI>
void BrwLock<PI>::ContendedReadAcquire() {
  // In the case where we wake other threads up we need them to not run until we're finished
  // holding the thread_lock, so disable local rescheduling.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    // Remove our optimistic reader from the count, and put a waiter on there instead.
    uint64_t prev =
        state_.state_.fetch_add(-kBrwLockReader + kBrwLockWaiter, ktl::memory_order_relaxed);
    // If there is a writer then we just block, they will wake us up
    if (prev & kBrwLockWriter) {
      Block(false);
      return;
    }
    // If we raced and there is in fact no one waiting then we can switch to
    // having the lock
    if ((prev & kBrwLockWaiterMask) == 0) {
      state_.state_.fetch_add(-kBrwLockWaiter + kBrwLockReader, ktl::memory_order_acquire);
      return;
    }
    // If there are no current readers then we need to wake somebody up
    if ((prev & kBrwLockReaderMask) == 1) {
      if (Wake() == ResourceOwnership::Reader) {
        // Join the reader pool.
        state_.state_.fetch_add(-kBrwLockWaiter + kBrwLockReader, ktl::memory_order_acquire);
        return;
      }
    }

    Block(false);
  }
}

template <BrwLockEnablePi PI>
void BrwLock<PI>::ContendedWriteAcquire() {
  // In the case where we wake other threads up we need them to not run until we're finished
  // holding the thread_lock, so disable local rescheduling.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    // Mark ourselves as waiting
    uint64_t prev = state_.state_.fetch_add(kBrwLockWaiter, ktl::memory_order_relaxed);
    // If there is a writer then we just block, they will wake us up
    if (prev & kBrwLockWriter) {
      Block(true);
      return;
    }
    if ((prev & kBrwLockReaderMask) == 0) {
      if ((prev & kBrwLockWaiterMask) == 0) {
        if constexpr (PI == BrwLockEnablePi::Yes) {
          state_.writer_.store(get_current_thread(), ktl::memory_order_relaxed);
        }
        // Must have raced previously as turns out there's no readers or
        // waiters, so we can convert to having the lock
        state_.state_.fetch_add(-kBrwLockWaiter + kBrwLockWriter, ktl::memory_order_acquire);
        return;
      } else {
        // There's no readers, but someone already waiting, wake up someone
        // before we ourselves block
        Wake();
      }
    }
    Block(true);
  }
}

template <BrwLockEnablePi PI>
void BrwLock<PI>::WriteRelease() {
  canary_.Assert();

#if LK_DEBUGLEVEL > 0
  if constexpr (PI == BrwLockEnablePi::Yes) {
    thread_t* holder = state_.writer_.load(ktl::memory_order_relaxed);
    thread_t* ct = get_current_thread();
    if (unlikely(ct != holder)) {
      panic(
          "BrwLock<PI>::WriteRelease: thread %p (%s) tried to release brwlock %p it "
          "doesn't "
          "own. Ownedby %p (%s)\n",
          ct, ct->name, this, holder, holder ? holder->name : "none");
    }
  }
#endif

  // For correct PI handling we need to ensure that up until a higher priority
  // thread can acquire the lock we will correctly be considered the owner.
  // Other threads are able to acquire the lock *after* we call ReleaseWakeup,
  // prior to that we could be racing with a higher priority acquirer and it
  // could be our responsibility to wake them up, and so up until ReleaseWakeup
  // is called they must be able to observe us as the owner.
  //
  // If we hold off on changing writer_ till after ReleaseWakeup we will then be
  // racing with others who may be acquiring, or be granted the write lock in
  // ReleaseWakeup, and so we would have to CAS writer_ to not clobber the new
  // holder. CAS is much more expensive than just a 'store', so to avoid that
  // we instead disable preemption. Disabling preemption effectively gives us the
  // highest priority, and so it is fine if acquirers observe writer_ to be null
  // and 'fail' to treat us as the owner.
  if constexpr (PI == BrwLockEnablePi::Yes) {
    thread_preempt_disable();

    state_.writer_.store(nullptr, ktl::memory_order_relaxed);
  }
  uint64_t prev = state_.state_.fetch_sub(kBrwLockWriter, ktl::memory_order_release);

  if (unlikely((prev & kBrwLockWaiterMask) != 0)) {
    // There are waiters, we need to wake them up
    ReleaseWakeup();
  }

  if constexpr (PI == BrwLockEnablePi::Yes) {
    thread_preempt_reenable();
  }
}

template <BrwLockEnablePi PI>
void BrwLock<PI>::ReleaseWakeup() {
  // Don't reschedule whilst we're waking up all the threads as if there are
  // several readers available then we'd like to get them all out of the wait queue.
  AutoReschedDisable resched_disable;
  resched_disable.Disable();
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    uint64_t count = state_.state_.load(ktl::memory_order_relaxed);
    if ((count & kBrwLockWaiterMask) != 0 && (count & kBrwLockWriter) == 0 &&
        (count & kBrwLockReaderMask) == 0) {
      Wake();
    }
  }
}

template <BrwLockEnablePi PI>
void BrwLock<PI>::ContendedReadUpgrade() {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // Convert our reading into waiting
  uint64_t prev =
      state_.state_.fetch_add(-kBrwLockReader + kBrwLockWaiter, ktl::memory_order_relaxed);
  if ((prev & ~kBrwLockWaiterMask) == kBrwLockReader) {
    if constexpr (PI == BrwLockEnablePi::Yes) {
      state_.writer_.store(get_current_thread(), ktl::memory_order_relaxed);
    }
    // There are no writers or readers. There might be waiters, but as we
    // already have some form of lock we still have fairness even if we
    // bypass the queue, so we convert our waiting into writing
    state_.state_.fetch_add(-kBrwLockWaiter + kBrwLockWriter, ktl::memory_order_acquire);
  } else {
    Block(true);
  }
}

template class BrwLock<BrwLockEnablePi::Yes>;
template class BrwLock<BrwLockEnablePi::No>;

}  // namespace internal
