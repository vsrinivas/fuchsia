// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012-2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Mutex functions
 *
 * @defgroup mutex Mutex
 * @{
 */

#include "kernel/mutex.h"

#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/affine/utils.h>
#include <lib/arch/intrin.h>
#include <lib/ktrace.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <kernel/auto_preempt_disabler.h>
#include <kernel/lock_trace.h>
#include <kernel/scheduler.h>
#include <kernel/task_runtime_timers.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/type_traits.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

namespace {

enum class KernelMutexTracingLevel {
  None,       // No tracing is ever done.  All code drops out at compile time.
  Contested,  // Trace events are only generated when mutexes are contested.
  All         // Trace events are generated for all mutex interactions.
};

// By default, kernel mutex tracing is disabled.
template <KernelMutexTracingLevel = KernelMutexTracingLevel::None, typename = void>
class KTracer;

template <>
class KTracer<KernelMutexTracingLevel::None> {
 public:
  KTracer() = default;
  void KernelMutexUncontestedAcquire(const Mutex* mutex) {}
  void KernelMutexUncontestedRelease(const Mutex* mutex) {}
  void KernelMutexBlock(const Mutex* mutex, Thread* blocker, uint32_t waiter_count) {}
  void KernelMutexWake(const Mutex* mutex, Thread* new_owner, uint32_t waiter_count) {}
};

template <KernelMutexTracingLevel Level>
class KTracer<Level, ktl::enable_if_t<(Level == KernelMutexTracingLevel::Contested) ||
                                      (Level == KernelMutexTracingLevel::All)>> {
 public:
  KTracer() : ts_(ktrace_timestamp()) {}

  void KernelMutexUncontestedAcquire(const Mutex* mutex) {
    if constexpr (Level == KernelMutexTracingLevel::All) {
      KernelMutexTrace(TAG_KERNEL_MUTEX_ACQUIRE, mutex, nullptr, 0);
    }
  }

  void KernelMutexUncontestedRelease(const Mutex* mutex) {
    if constexpr (Level == KernelMutexTracingLevel::All) {
      KernelMutexTrace(TAG_KERNEL_MUTEX_RELEASE, mutex, nullptr, 0);
    }
  }

  void KernelMutexBlock(const Mutex* mutex, const Thread* blocker, uint32_t waiter_count) {
    KernelMutexTrace(TAG_KERNEL_MUTEX_BLOCK, mutex, blocker, waiter_count);
  }

  void KernelMutexWake(const Mutex* mutex, const Thread* new_owner, uint32_t waiter_count) {
    KernelMutexTrace(TAG_KERNEL_MUTEX_RELEASE, mutex, new_owner, waiter_count);
  }

 private:
  void KernelMutexTrace(uint32_t tag, const Mutex* mutex, const Thread* t, uint32_t waiter_count) {
    auto tid_type = fxt::StringRef{(t == nullptr                  ? "none"_stringref
                                    : t->user_thread() == nullptr ? "kernel_mode"_stringref
                                                                  : "user_mode"_stringref)
                                       ->GetFxtId()};

    fxt::Argument<fxt::ArgumentType::kPointer, fxt::RefType::kId> mutex_id_arg{
        fxt::StringRef{"mutex_id"_stringref->GetFxtId()}, reinterpret_cast<uintptr_t>(mutex)};
    fxt::Argument<fxt::ArgumentType::kKoid, fxt::RefType::kId> tid_name_arg{
        fxt::StringRef{"tid"_stringref->GetFxtId()}, t == nullptr ? ZX_KOID_INVALID : t->tid()};
    fxt::Argument tid_type_arg{fxt::StringRef{"tid_type"_stringref->GetFxtId()}, tid_type};
    fxt::Argument wait_count_arg{fxt::StringRef{"waiter_count"_stringref->GetFxtId()},
                                 waiter_count};

    auto event_name = fxt::StringRef{(tag == TAG_KERNEL_MUTEX_ACQUIRE   ? "mutex_acquire"_stringref
                                      : tag == TAG_KERNEL_MUTEX_RELEASE ? "mutex_release"_stringref
                                      : tag == TAG_KERNEL_MUTEX_BLOCK   ? "mutex_block"_stringref
                                                                        : "unknown"_stringref)
                                         ->GetFxtId()};

    fxt_duration_complete(tag, ts_, fxt::ThreadRef{t->pid(), t->tid()},
                          fxt::StringRef{"kernel:sched"_stringref->GetFxtId()}, event_name,
                          ts_ + 50, mutex_id_arg, tid_name_arg, tid_type_arg, wait_count_arg);
  }

  const uint64_t ts_;
};

}  // namespace

Mutex::~Mutex() {
  magic_.Assert();
  DEBUG_ASSERT(!arch_blocking_disallowed());

  if (LK_DEBUGLEVEL > 0) {
    if (val() != STATE_FREE) {
      Thread* h = holder();
      panic("~Mutex(): thread %p (%s) tried to destroy locked mutex %p, locked by %p (%s)\n",
            Thread::Current::Get(), Thread::Current::Get()->name(), this, h, h->name());
    }
  }

  val_.store(STATE_FREE, ktl::memory_order_relaxed);
}

// By parameterizing on whether we're going to set a timeslice extension or not
// we can shave a few cycles.
template <bool TimesliceExtensionEnabled>
bool Mutex::AcquireCommon(zx_duration_t spin_max_duration,
                          TimesliceExtension<TimesliceExtensionEnabled> timeslice_extension) {
  magic_.Assert();
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);

  Thread* const current_thread = Thread::Current::Get();
  const uintptr_t new_mutex_state = reinterpret_cast<uintptr_t>(current_thread);

  {
    // Make sure that we don't leave this scope with preemption disabled.
    AutoPreemptDisabler preempt_disabler(AutoPreemptDisabler::Defer);
    if constexpr (TimesliceExtensionEnabled) {
      // We've got a timeslice extension that we need to install after we've
      // acquired the mutex.  However, to avoid the (small) risk of getting
      // preempted after acquiring the mutex, but before we've installed the
      // timeslice extension, disable preemption.
      preempt_disabler.Disable();
    }

    // Fast path: The mutex is unlocked and uncontested. Try to acquire it immediately.
    //
    // We use the weak form of compare exchange here, which is faster on some
    // architectures (e.g. aarch64). In the rare case it spuriously fails, the slow
    // path will handle it.
    uintptr_t old_mutex_state = STATE_FREE;
    if (likely(val_.compare_exchange_weak(old_mutex_state, new_mutex_state,
                                          ktl::memory_order_acquire, ktl::memory_order_relaxed))) {
      RecordInitialAssignedCpu();

      // TODO(maniscalco): Is this the right place to put the KTracer?  Seems like
      // it should be the very last thing we do.
      //
      // Don't bother to update the ownership of the wait queue. If another thread
      // attempts to acquire the mutex and discovers it to be already locked, it
      // will take care of updating the wait queue ownership while it is inside of
      // the thread_lock.
      KTracer{}.KernelMutexUncontestedAcquire(this);

      if constexpr (TimesliceExtensionEnabled) {
        return Thread::Current::preemption_state().SetTimesliceExtension(timeslice_extension.value);
      }
      return false;
    }
  }

  return AcquireContendedMutex(spin_max_duration, current_thread, timeslice_extension);
}

template <bool TimesliceExtensionEnabled>
__NO_INLINE bool Mutex::AcquireContendedMutex(
    zx_duration_t spin_max_duration, Thread* current_thread,
    TimesliceExtension<TimesliceExtensionEnabled> timeslice_extension) {
  LOCK_TRACE_DURATION("Mutex::AcquireContended");

  // It looks like the mutex is most likely contested (at least, it was when we
  // just checked). Enter the adaptive mutex spin phase, where we spin on the
  // mutex hoping that the thread which owns the mutex is running on a different
  // CPU, and will release the mutex shortly.
  //
  // If we manage to acquire the mutex during the spin phase, we can simply
  // exit, having achieved our goal.  Otherwise, there are 3 reasons we may end
  // up terminating the spin phase and dropping into a block operation.
  //
  // 1) We exceed the system's configured |spin_max_duration|.
  // 2) The mutex is marked as CONTESTED, meaning that at least one other thread
  //    has dropped out of its spin phase and blocked on the mutex.
  // 3) We think that there is a reasonable chance that the owner of this mutex
  //    was assigned to the same core that we are running on.
  //
  // Notes about #3:
  //
  // In order to implement this behavior, the Mutex class maintains a variable
  // called |maybe_acquired_on_cpu_|.  This is the system's best guess as to
  // which CPU the owner of the mutex may currently be assigned to. The value of
  // the variable is set when a thread successfully acquires the mutex, and
  // cleared when the thread releases the mutex later on.
  //
  // This behavior is best effort; the guess is just a guess and could be wrong
  // for several legitimate reasons.  The owner of the mutex will assign the
  // variable to the value of the CPU is it running on immediately after it
  // successfully mutates the mutex state to indicate that it owns the mutex.
  //
  // A spinning thread my observe:
  // 1) A value of INVALID_CPU, either because of weak memory ordering, or
  //    because the thread was preempted after updating the mutex state, but
  //    before recording the assigned CPU guess.
  // 2) An incorrect value of the assigned CPU, again either because of weak
  //    memory ordering, or because the thread either moved to a different CPU
  //    or blocked after the guess was recorded.
  //
  // So, it is possible to keep spinning when we probably shouldn't, and also
  // possible to drop out of a spin when we might want to stay in it.
  //
  // TODO(fxbug.dev/34646): Optimize cache pressure of spinners and default spin max.

  const uintptr_t new_mutex_state = reinterpret_cast<uintptr_t>(current_thread);

  // Make sure that we don't leave this scope with preemption disabled.  If
  // we've got a timeslice extension, we're going to disable preemption while
  // spinning to ensure that we can't get "preempted early" if we end up
  // acquiring the mutex in the spin phase.  However, if a preemption becomes
  // pending while spinning, we'll briefly enable then disable preemption to
  // allow a reschedule.
  AutoPreemptDisabler preempt_disabler(AutoPreemptDisabler::Defer);
  if constexpr (TimesliceExtensionEnabled) {
    preempt_disabler.Disable();
  }

  // Remember the last call to current_ticks.
  zx_ticks_t now_ticks = current_ticks();

  const affine::Ratio time_to_ticks = platform_get_ticks_to_time_ratio().Inverse();
  const zx_ticks_t spin_until_ticks =
      affine::utils::ClampAdd(now_ticks, time_to_ticks.Scale(spin_max_duration));
  do {
    uintptr_t old_mutex_state = STATE_FREE;
    // Attempt to acquire the mutex by swapping out "STATE_FREE" for our current thread.
    //
    // We use the weak form of compare exchange here: it saves an extra
    // conditional branch on ARM, and if it fails spuriously, we'll just
    // loop around and try again.
    //
    if (likely(val_.compare_exchange_weak(old_mutex_state, new_mutex_state,
                                          ktl::memory_order_acquire, ktl::memory_order_relaxed))) {
      RecordInitialAssignedCpu();

      // Same as above in the fastest path: leave accounting to later contending
      // threads.
      KTracer{}.KernelMutexUncontestedAcquire(this);

      if constexpr (TimesliceExtensionEnabled) {
        return Thread::Current::preemption_state().SetTimesliceExtension(timeslice_extension.value);
      }
      return false;
    }

    // Stop spinning if the mutex is or becomes contested. All spinners convert
    // to blocking when the first one reaches the max spin duration.
    if (old_mutex_state & STATE_FLAG_CONTESTED) {
      break;
    }

    {
      // Stop spinning if it looks like we might be running on the same CPU which
      // was assigned to the owner of the mutex.
      //
      // Note: The accuracy of |curr_cpu_num| depends on whether preemption is
      // currently enabled or not and whether we re-enable it below.
      const cpu_num_t curr_cpu_num = arch_curr_cpu_num();
      if (curr_cpu_num == maybe_acquired_on_cpu_.load(ktl::memory_order_relaxed)) {
        break;
      }

      if constexpr (TimesliceExtensionEnabled) {
        // If this CPU has a preemption pending, briefly enable then disable
        // preemption to give this CPU a chance to reschedule.
        const cpu_mask_t curr_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
        if ((Thread::Current::preemption_state().preempts_pending() & curr_cpu_mask) != 0) {
          // Reenable preemption to trigger a local reschedule and then disable it again.
          preempt_disabler.Enable();
          preempt_disabler.Disable();
        }
      }
    }

    // Give the arch a chance to relax the CPU.
    arch::Yield();
    now_ticks = current_ticks();
  } while (now_ticks < spin_until_ticks);

  if ((LK_DEBUGLEVEL > 0) && unlikely(this->IsHeld())) {
    panic("Mutex::Acquire: thread %p (%s) tried to acquire mutex %p it already owns.\n",
          current_thread, current_thread->name(), this);
  }

  ContentionTimer timer(current_thread, now_ticks);

  // |OwnedWaitQueue::BlockAndAssignOwner| requires that preemption be disabled.
  preempt_disabler.Disable();

  {
    // we contended with someone else, will probably need to block
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

    // Check if the queued flag is currently set. The contested flag can only be changed
    // whilst the thread lock is held so we know we aren't racing with anyone here. This
    // is just an optimization and allows us to avoid redundantly doing the atomic OR.
    uintptr_t old_mutex_state = val();

    if (unlikely(!(old_mutex_state & STATE_FLAG_CONTESTED))) {
      // Set the queued flag to indicate that we're blocking.
      //
      // We may find the old state was |STATE_FREE| if we raced with the
      // holder as they dropped the mutex. We use the |acquire| memory ordering
      // in the |fetch_or| just in case this happens, to ensure we see the memory
      // released by the previous lock holder.
      old_mutex_state = val_.fetch_or(STATE_FLAG_CONTESTED, ktl::memory_order_acquire);
      if (unlikely(old_mutex_state == STATE_FREE)) {
        // Since we set the contested flag we know that there are no
        // waiters and no one is able to perform fast path acquisition.
        // Therefore we can just take the mutex, and remove the queued
        // flag.
        val_.store(new_mutex_state, ktl::memory_order_relaxed);
        RecordInitialAssignedCpu();

        if constexpr (TimesliceExtensionEnabled) {
          return Thread::Current::preemption_state().SetTimesliceExtension(
              timeslice_extension.value);
        }
        return false;
      }
    }

    const uint64_t flow_id = current_thread->TakeNextLockFlowId();
    LOCK_TRACE_FLOW_BEGIN("contend_mutex", flow_id);

    // extract the current holder of the mutex from oldval, no need to
    // re-read from the mutex as it cannot change if the queued flag is set
    // without holding the thread lock (which we currently hold).  We need
    // to be sure that we inform our owned wait queue that this is the
    // proper queue owner as we block.
    Thread* cur_owner = holder_from_val(old_mutex_state);
    KTracer{}.KernelMutexBlock(this, cur_owner, wait_.Count() + 1);
    zx_status_t ret = wait_.BlockAndAssignOwner(Deadline::infinite(), cur_owner,
                                                ResourceOwnership::Normal, Interruptible::No);

    if (unlikely(ret < ZX_OK)) {
      // mutexes are not interruptible and cannot time out, so it
      // is illegal to return with any error state.
      panic("Mutex::Acquire: wait queue block returns with error %d m %p, thr %p, sp %p\n", ret,
            this, current_thread, __GET_FRAME());
    }

    // someone must have woken us up, we should own the mutex now
    DEBUG_ASSERT(current_thread == holder());

    LOCK_TRACE_FLOW_END("contend_mutex", flow_id);
  }

  if constexpr (TimesliceExtensionEnabled) {
    return Thread::Current::preemption_state().SetTimesliceExtension(timeslice_extension.value);
  }
  return false;
}

inline uintptr_t Mutex::TryRelease(Thread* current_thread) {
  // Try the fast path.  Assume that we are locked, but uncontested.
  uintptr_t old_mutex_state = reinterpret_cast<uintptr_t>(current_thread);
  if (likely(val_.compare_exchange_strong(old_mutex_state, STATE_FREE, ktl::memory_order_release,
                                          ktl::memory_order_relaxed))) {
    // We're done.  Since this mutex was uncontested, we know that we were
    // not receiving any priority pressure from the wait queue, and there is
    // nothing further to do.
    KTracer{}.KernelMutexUncontestedRelease(this);
    return STATE_FREE;
  }

  // The mutex is contended, return the current state of the mutex.
  return old_mutex_state;
}

__NO_INLINE void Mutex::ReleaseContendedMutex(Thread* current_thread, uintptr_t old_mutex_state) {
  LOCK_TRACE_DURATION("Mutex::ReleaseContended");

  // Sanity checks.  The mutex should have been either locked by us and
  // uncontested, or locked by us and contested.  Anything else is an internal
  // consistency error worthy of a panic.
  if (LK_DEBUGLEVEL > 0) {
    uintptr_t expected_state = reinterpret_cast<uintptr_t>(current_thread) | STATE_FLAG_CONTESTED;

    if (unlikely(old_mutex_state != expected_state)) {
      auto other_holder = reinterpret_cast<Thread*>(old_mutex_state & ~STATE_FLAG_CONTESTED);
      panic(
          "Mutex::ReleaseContendedMutex: sanity check failure.  Thread %p (%s) tried to release "
          "mutex %p.  Expected state (%lx) != observed state (%lx).  Other holder (%s)\n",
          current_thread, current_thread->name(), this, expected_state, old_mutex_state,
          other_holder ? other_holder->name() : "<none>");
    }
  }

  // Attempt to release a thread. If there are still waiters in the queue
  // after we successfully have woken a thread, be sure to assign ownership of
  // the queue to the thread which was woken so that it can properly receive
  // the priority pressure of the remaining waiters.
  using Action = OwnedWaitQueue::Hook::Action;
  Thread* woken;
  auto cbk = [](Thread* woken, void* ctx) -> Action {
    *(reinterpret_cast<Thread**>(ctx)) = woken;
    return Action::SelectAndAssignOwner;
  };

  KTracer tracer;
  wait_.WakeThreads(1, {cbk, &woken});
  tracer.KernelMutexWake(this, woken, wait_.Count());

  // So, the mutex is now in one of three states.  It can be...
  //
  // 1) Owned and contested (we woke a thread up, and there are still waiters)
  // 2) Owned and uncontested (we woke a thread up, but it was the last one)
  // 3) Unowned (no thread woke up when we tried to wake one)
  //
  // Note, the only way to be in situation #3 is for the lock to have become
  // contested at some point in the past, but then to have a thread stop
  // waiting for the lock before acquiring it (either it timed out or was
  // killed).
  //
  uintptr_t new_mutex_state;
  if (woken != nullptr) {
    LOCK_TRACE_FLOW_STEP("contend_mutex", woken->lock_flow_id());

    // We woke _someone_ up.  We be in situation #1 or #2
    new_mutex_state = reinterpret_cast<uintptr_t>(woken);
    if (!wait_.IsEmpty()) {
      // Situation #1.
      DEBUG_ASSERT(wait_.owner() == woken);
      new_mutex_state |= STATE_FLAG_CONTESTED;
    } else {
      // Situation #2.
      DEBUG_ASSERT(wait_.owner() == nullptr);
    }
  } else {
    DEBUG_ASSERT(wait_.IsEmpty());
    DEBUG_ASSERT(wait_.owner() == nullptr);
    new_mutex_state = STATE_FREE;
  }

  if (unlikely(!val_.compare_exchange_strong(old_mutex_state, new_mutex_state,
                                             ktl::memory_order_release,
                                             ktl::memory_order_relaxed))) {
    panic("bad state (%lx != %lx) in mutex release %p, current thread %p\n",
          reinterpret_cast<uintptr_t>(current_thread) | STATE_FLAG_CONTESTED, old_mutex_state, this,
          current_thread);
  }
}

void Mutex::Release() {
  magic_.Assert();
  DEBUG_ASSERT(!arch_blocking_disallowed());
  Thread* current_thread = Thread::Current::Get();

  ClearInitialAssignedCpu();

  if (const uintptr_t old_mutex_state = TryRelease(current_thread); old_mutex_state != STATE_FREE) {
    // Disable preemption to prevent switching to the woken thread inside of
    // WakeThreads() if it is assigned to this CPU. If the woken thread is
    // assigned to a different CPU, the thread lock prevents it from observing
    // the inconsistent owner before the correct owner is recorded.
    AnnotatedAutoPreemptDisabler preempt_disable;
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    ReleaseContendedMutex(current_thread, old_mutex_state);
  }
}

void Mutex::ReleaseThreadLocked() {
  magic_.Assert();
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT(arch_ints_disabled());
  preempt_disabled_token.AssertHeld();
  thread_lock.AssertHeld();
  Thread* current_thread = Thread::Current::Get();

  ClearInitialAssignedCpu();

  if (const uintptr_t old_mutex_state = TryRelease(current_thread); old_mutex_state != STATE_FREE) {
    ReleaseContendedMutex(current_thread, old_mutex_state);
  }
}

// Explicit instantiations since it's not defined in the header.
template bool Mutex::AcquireCommon(zx_duration_t spin_max_duration, TimesliceExtension<false>);
template bool Mutex::AcquireCommon(zx_duration_t spin_max_duration, TimesliceExtension<true>);
