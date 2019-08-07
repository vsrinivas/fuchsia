// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/internal/mutex-internal.h>
#include <stdatomic.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

// This mutex implementation is based on Ulrich Drepper's paper "Futexes
// Are Tricky" (dated November 5, 2011; see
// http://www.akkadia.org/drepper/futex.pdf).  We use the approach from
// "Mutex, Take 2", with one modification: We use an atomic swap in
// sync_mutex_unlock() rather than an atomic decrement.

// On success, this will leave the mutex in the LOCKED_WITH_WAITERS state.
static zx_status_t lock_slow_path(sync_mutex_t* mutex, zx_time_t deadline,
                                  zx_futex_storage_t owned_and_contested_val,
                                  zx_futex_storage_t old_state) {
  for (;;) {
    // If the state shows the futex is already contested, or we can update
    // it to indicate this, then wait.  Make sure that we tell the kernel
    // who we think is holding the mutex (and therefore "owns" the futex) as
    // we do so.
    const zx_futex_storage_t contested_state = libsync_mutex_make_contested(old_state);
    if ((contested_state == old_state) ||
        atomic_compare_exchange_strong(&mutex->futex, &old_state, contested_state)) {
      zx_status_t status = _zx_futex_wait(&mutex->futex, contested_state,
                                          libsync_mutex_make_owner_from_state(old_state), deadline);
      if (status == ZX_ERR_TIMED_OUT)
        return ZX_ERR_TIMED_OUT;
    }

    // Try again to claim the mutex.  On this try, we must set the mutex
    // state to indicate that it is locked and owned by us, and contested.
    // This is because we don't actually know if there are still waiters in
    // the futex or not.  When we get around to unlocking, we will need to
    // try to release a waiter, just in case.
    old_state = LIB_SYNC_MUTEX_UNLOCKED;
    if (atomic_compare_exchange_strong(&mutex->futex, &old_state, owned_and_contested_val)) {
      return ZX_OK;
    }
  }
}

zx_status_t sync_mutex_trylock(sync_mutex_t* mutex) {
  zx_futex_storage_t old_state = LIB_SYNC_MUTEX_UNLOCKED;
  if (atomic_compare_exchange_strong(&mutex->futex, &old_state,
                                     libsync_mutex_locked_and_uncontested())) {
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

zx_status_t sync_mutex_timedlock(sync_mutex_t* mutex, zx_time_t deadline) {
  // Try to claim the mutex.  This compare-and-swap executes the full
  // memory barrier that locking a mutex is required to execute.
  zx_futex_storage_t old_state = LIB_SYNC_MUTEX_UNLOCKED;
  zx_futex_storage_t uncontested = libsync_mutex_locked_and_uncontested();
  if (atomic_compare_exchange_strong(&mutex->futex, &old_state, uncontested)) {
    return ZX_OK;
  }
  return lock_slow_path(mutex, deadline, libsync_mutex_make_contested(uncontested), old_state);
}

void sync_mutex_lock(sync_mutex_t* mutex) __TA_NO_THREAD_SAFETY_ANALYSIS {
  zx_status_t status = sync_mutex_timedlock(mutex, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    __builtin_trap();
  }
}

void sync_mutex_lock_with_waiter(sync_mutex_t* mutex) __TA_NO_THREAD_SAFETY_ANALYSIS {
  zx_futex_storage_t old_state = LIB_SYNC_MUTEX_UNLOCKED;
  zx_futex_storage_t contested =
      libsync_mutex_make_contested(libsync_mutex_locked_and_uncontested());

  if (atomic_compare_exchange_strong(&mutex->futex, &old_state, contested)) {
    return;
  }

  zx_status_t status = lock_slow_path(mutex, ZX_TIME_INFINITE, contested, old_state);
  if (status != ZX_OK) {
    __builtin_trap();
  }
}

void sync_mutex_unlock(sync_mutex_t* mutex) __TA_NO_THREAD_SAFETY_ANALYSIS {
  // Attempt to release the mutex.  This atomic swap executes the full
  // memory barrier that unlocking a mutex is required to execute.
  zx_futex_storage_t old_state = atomic_exchange(&mutex->futex, LIB_SYNC_MUTEX_UNLOCKED);

  // At this point, the mutex is unlocked.  In some usage patterns (e.g. for
  // reference counting), another thread might now acquire the mutex and free
  // the memory containing it.  This means we must not dereference |mutex|
  // from this point onwards.
  if (unlikely(old_state == LIB_SYNC_MUTEX_UNLOCKED)) {
    // Either the mutex was unlocked (in which case the unlock call
    // was invalid), or the mutex was in an invalid state.
    __builtin_trap();
  } else {
    if (libsync_mutex_is_contested(old_state)) {
      // Note that the mutex's memory could have been freed and reused by
      // this point, so this could cause a spurious futex wakeup for a
      // unrelated user of the memory location.
      //
      // With that said, this is almost certainly a user error, as their
      // code allowed mutex to destruct while it still had waiters.  The
      // only way for this to _not_ be true would be if all of the waiters
      // who had been waiting were either killed or had timed out while
      // waiting.
      zx_status_t status = _zx_futex_wake_single_owner(&mutex->futex);
      if (status != ZX_OK) {
        __builtin_trap();
      }
    }
  }
}
