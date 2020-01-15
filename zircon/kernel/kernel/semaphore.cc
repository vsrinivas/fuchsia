// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "kernel/semaphore.h"

#include <err.h>
#include <zircon/compiler.h>

#include <kernel/thread_lock.h>

void Semaphore::Post() {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // Either the number of waiters in the wait queue, or the semaphore count,
  // must be 0.  It should never be possible for there to be waiters, and a
  // positive count.
  DEBUG_ASSERT((count_ == 0) || waitq_.IsEmpty());

  // If we have no waiters, increment the count.  Otherwise, release a waiter.
  if (waitq_.IsEmpty()) {
    ++count_;
  } else {
    waitq_.WakeOne(true, ZX_OK);
  }
}

zx_status_t Semaphore::Wait(const Deadline& deadline) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  DEBUG_ASSERT((count_ == 0) || waitq_.IsEmpty());

  // If the count is positive, simply decrement it and get out.
  if (count_ > 0) {
    --count_;
    return ZX_OK;
  }

  // Wait in an interruptible state.  We will either be woken by a Post
  // operation, or by a timeout or signal.  Whatever happens, return the reason
  // the wait operation ended.
  thread_t* current_thread = get_current_thread();
  current_thread->interruptable = true;
  zx_status_t ret = waitq_.Block(deadline);
  current_thread->interruptable = false;
  return ret;
}
