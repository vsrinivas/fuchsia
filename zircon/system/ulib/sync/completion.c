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

  // There are only two choices here.  The previous state was
  // UNSIGNALED_WITH_WAITERS (and we changed nothing), or it was UNSIGNALED
  // (and we just transitioned it to UWW).  Either way, we expect the state to
  // be UWW by the time we join the wait queue.  If it is anything else
  // (BAD_STATE), then it must have achieved SIGNALED at some point in the
  // past.  Likewise (ignoring the race described below), if we get ZX_OK
  // back, then we must have been woken by some other thread which was
  // signaling our completion.
  switch (_zx_futex_wait(futex, UNSIGNALED_WITH_WAITERS, ZX_HANDLE_INVALID, deadline)) {
    case ZX_OK:
    case ZX_ERR_BAD_STATE:
      return ZX_OK;

    case ZX_ERR_TIMED_OUT:
      return ZX_ERR_TIMED_OUT;

    case ZX_ERR_INVALID_ARGS:
    default:
      __builtin_trap();
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
