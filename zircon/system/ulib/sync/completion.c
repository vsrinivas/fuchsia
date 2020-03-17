// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <limits.h>
#include <stdatomic.h>
#include <zircon/syscalls.h>

enum {
  UNSIGNALED = 0,
  UNSIGNALED_WITH_WAITERS = 1,
  SIGNALED = 2,
};

zx_status_t sync_completion_wait(sync_completion_t* completion, zx_duration_t timeout) {
  zx_time_t deadline =
      (timeout == ZX_TIME_INFINITE) ? ZX_TIME_INFINITE : zx_deadline_after(timeout);
  return sync_completion_wait_deadline(completion, deadline);
}

zx_status_t sync_completion_wait_deadline(sync_completion_t* completion, zx_time_t deadline) {
  // TODO(kulakowski): With a little more state (a waiters count),
  // this could optimistically spin before entering the kernel.

  atomic_int* futex = &completion->futex;
  int32_t prev_value = UNSIGNALED;

  atomic_compare_exchange_strong(futex, &prev_value, UNSIGNALED_WITH_WAITERS);

  // If we had been signaled, then just get out.  Because of the CMPX, we are
  // still signaled.
  if (prev_value == SIGNALED) {
    return ZX_OK;
  }

  while (true) {
    switch (_zx_futex_wait(futex, UNSIGNALED_WITH_WAITERS, ZX_HANDLE_INVALID, deadline)) {
      case ZX_OK:
        // We just woke up because of an explicit zx_futex_wake which found us
        // waiting in this futex's wait queue.  Verify that the state is
        // something _other_ than UNSIGNALED_WITH_WAITERS.  If it is, then we
        // must have been signaled at some point in the past.
        //
        // If not, then there is one of two things going on.  The common
        // possibility is that we got hit with a lingering, in-flight, futex
        // wake operation.  The common flow would be as follows (although, many
        // variants could exist).
        //
        // Given two threads, T1 and T2, and a completion C.
        //
        // 1) T1 calls C.wait, and has made it to this point.  It is calling
        //    futex_wait, but has not made it all of the way into the kernel.
        // 2) T2 calls C.signal.  It has swapped the state from UWW to S, and it
        //    is about to call futex_wake.
        // 3) T1 makes it into zx_futex_wait and fails the state check.  The
        //    state is now SIGNALLED.  T1 wakes and unwinds, as it should since
        //    it was signalled.
        // 4) T1 either destroys and recreates C at the same memory location, or
        //    it simply resets C.  The state of C is now UNSIGNALED
        // 5) T1 fully waits on C.  The state is now UWW, and T1 has made it all
        //    of the way into the kernel, passed the futex state check, and joined
        //    the kernel wait queue.
        // 6) T2 finally runs again.  Its call to futex_wake causes T1 to wake
        //    up.
        //
        // Without the check below, T1 is going to wake in a spurious fashion,
        // which we really do not want.  With the check, T1 will see the state
        // as _still_ being UWW, and it will try again.
        //
        // Another scenario might go like this.
        //
        // 1) Start with T1 currently parked in C.  It is fully in the
        //    kernel wait queue and the state of C is UWW.
        // 2) T2 calls signal and makes it all of the way through the process.
        //    It has exited the signal function, and the state of the C is UWW.
        //    T1 has been released from the wait queue and is in the process of
        //    unwinding.
        // 3) Some other thread T3, (T3 could == T2, does not have to) calls reset,
        //    then wait.  C's state is now UWW and T3 is on the way down to join
        //    the kernel wait queue.
        // 4) T1 unwinds fully, it makes the check here, and loops back around
        //    going to sleep again.
        //
        // In this case, we appear to have "missed" the event.
        //
        // So, with all of that said, the currently defined proper behavior is
        // to "miss" the event.  This has been pretty thoroughly debated, and
        // the short answer is that disallowing this type of spurious wakeup is
        // more valuable than disallowing any sort of "missed" signal.  If you
        // have code which depends on never "missing" a signal in the fashion
        // described above, you should use a different synchronization primitive
        // than this one.
        //
        if (atomic_load_explicit(futex, memory_order_acquire) != UNSIGNALED_WITH_WAITERS) {
          return ZX_OK;
        }
        break;

      // There are only two choices here.  The previous state was
      // UNSIGNALED_WITH_WAITERS (and we changed nothing), or it was UNSIGNALED
      // (and we just transitioned it to UWW).  Either way, we expect the state to
      // be UWW by the time we join the wait queue.  If it is anything else
      // (BAD_STATE), then it must have achieved SIGNALED at some point in the
      // past.
      //
      // Before we exit, however, we toss in an explicit acquire fence.  This is
      // needed to provide the acquire semantics that we guarantee in the
      // documentation of sync_completion.  More concretely, it means that load
      // operations which take place after this wait operation cannot be moved
      // (either by the compiler or the hardware) before this point in our
      // program.  The fence is a slightly stronger guarantee then we actually
      // need, but we would rather not run the risk that an aggressive compiler
      // decides that it wants to optimize away the load-acquire operation for
      // some reason.
      case ZX_ERR_BAD_STATE:
        atomic_thread_fence(memory_order_acquire);
        return ZX_OK;

      case ZX_ERR_TIMED_OUT:
        return ZX_ERR_TIMED_OUT;

      case ZX_ERR_INVALID_ARGS:
      default:
        __builtin_trap();
    }
  }
}

void sync_completion_signal(sync_completion_t* completion) {
  atomic_int* futex = &completion->futex;
  int32_t expected = atomic_load_explicit(futex, memory_order_acquire);

  do {
    if (expected == SIGNALED) {
      return;
    }

    // ASSERT that the state was either or UNSIGNALED or
    // UNSIGNALED_WITH_WAITERS.  Anything else is an indication of either a bad
    // pointer being passed to us, or memory corruption.
    if ((expected != UNSIGNALED) && (expected != UNSIGNALED_WITH_WAITERS)) {
      __builtin_trap();
    }

    // Exchange what was with SIGNALED.  If we fail, just restart.
  } while (!atomic_compare_exchange_weak_explicit(futex, &expected, SIGNALED, memory_order_seq_cst,
                                                  memory_order_acquire));

  // Success!  If there had been waiters, wake them up now.
  if (expected == UNSIGNALED_WITH_WAITERS) {
    _zx_futex_wake(futex, UINT32_MAX);
  }
}

void sync_completion_signal_requeue(sync_completion_t* completion, zx_futex_t* requeue_target,
                                    zx_handle_t requeue_target_owner) {
  atomic_store(&completion->futex, SIGNALED);
  // Note that _zx_futex_requeue() will check the value of &completion->futex
  // and return ZX_ERR_BAD_STATE if it is not SIGNALED. The only way that could
  // happen is racing with sync_completion_reset(). This is not an intended use
  // case for this function: we only expect it to be used internally by libsync
  // and without sync_completion_reset().
  //
  // However, if this theoretical scenario actually occurs, we can still safely
  // ignore the error: there is no point in waking up the waiters since they
  // would find an UNSIGNALED value and go back to sleep.
  _zx_futex_requeue(&completion->futex, 0, SIGNALED, requeue_target, UINT32_MAX,
                    requeue_target_owner);
}

void sync_completion_reset(sync_completion_t* completion) {
  atomic_int* futex = &completion->futex;
  int expected = SIGNALED;

  if (!atomic_compare_exchange_strong(futex, &expected, UNSIGNALED)) {
    // If we were not SIGNALED, then we had better have been with either
    // UNSIGNALED, or UNSIGNALED_WITH_WAITERS.  Anything else is an indication
    // of either a bad pointer being passed, or corruption.
    if ((expected != UNSIGNALED) && (expected != UNSIGNALED_WITH_WAITERS)) {
      __builtin_trap();
    }
  }
}

bool sync_completion_signaled(sync_completion_t* completion) {
  return atomic_load_explicit(&completion->futex, memory_order_acquire) == SIGNALED;
}
