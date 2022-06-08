// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync_obj.h"

#include <zircon/assert.h>

#include "global_stats.h"
#include "random.h"

zx::time SyncObj::GetAcqTimeout(TestThreadBehavior& behavior) {
  return Random::RollDice(behavior.timeout_prob)
             ? zx::deadline_after(zx::nsec(Random::Get(behavior.timeout_dist)))
             : zx::time::infinite();
}

void MutexSyncObj::Acquire(TestThreadBehavior& behavior) {
  // Figure out if we are using a timeout when acquiring the mutex, and compute
  // the deadline if we are.
  zx::time deadline{GetAcqTimeout(behavior)};

  if (deadline != zx::time::infinite()) {
    zx_status_t res = sync_mutex_timedlock(&mutex_, deadline.get());

    // If we got the lock, then great.  We are done.
    if (res == ZX_OK) {
      sync_mutex_assert_held(&mutex_);
      return;
    }

    // Otherwise, the error had better be ZX_ERR_TIMED_OUT.  Record the fact
    // that we timed out during a mutex acquire operation in the global stats,
    // then fall through to a plain-old acquire with no timeout.
    ZX_ASSERT(res == ZX_ERR_TIMED_OUT);
    global_stats.mutex_acq_timeouts.fetch_add(1);
  }

  sync_mutex_lock(&mutex_);
  global_stats.mutex_acquires.fetch_add(1);
}

void MutexSyncObj::Release() {
  sync_mutex_unlock(&mutex_);
  global_stats.mutex_releases.fetch_add(1);
}

void CondVarSyncObj::Acquire(TestThreadBehavior& behavior) {
  zx::time deadline{GetAcqTimeout(behavior)};

  sync_mutex_lock(&mutex_);

  while (!shutdown_now_) {
    if (!owner_.has_value()) {
      owner_ = thrd_current();
      global_stats.condvar_acquires.fetch_add(1u);
      break;
    } else {
      ZX_ASSERT(owner_.value() != thrd_current());
    }

    global_stats.condvar_waits.fetch_add(1u);

    if (deadline == zx::time::infinite()) {
      sync_condition_wait(&the_condition_, &mutex_);
    } else {
      // Looks like we have a deadline.  Do a timed wait, and then (if we time
      // out), clear the deadline so that next time we wait unconditionally.
      zx_status_t res = sync_condition_timedwait(&the_condition_, &mutex_, deadline.get());
      if (res != ZX_OK) {
        ZX_ASSERT(res == ZX_ERR_TIMED_OUT);
        deadline = zx::time::infinite();
        global_stats.condvar_acq_timeouts.fetch_add(1);
      }
    }
  }

  sync_mutex_unlock(&mutex_);
}

void CondVarSyncObj::Release() {
  // Signal the condition.  Flip a coin to determine if we use a single wait
  // or a broadcast.
  sync_mutex_lock(&mutex_);

  if (!shutdown_now_) {
    ZX_ASSERT(owner_.has_value());
    ZX_ASSERT(owner_.value() == thrd_current());
  }
  owner_.reset();

  if (Random::RollDice(0.5)) {
    global_stats.condvar_signals.fetch_add(1u);
    sync_condition_signal(&the_condition_);
  } else {
    global_stats.condvar_bcasts.fetch_add(1u);
    sync_condition_broadcast(&the_condition_);
  }

  sync_mutex_unlock(&mutex_);
  global_stats.condvar_releases.fetch_add(1u);
}

void CondVarSyncObj::Shutdown() {
  sync_mutex_lock(&mutex_);
  shutdown_now_ = true;
  owner_.reset();
  sync_condition_broadcast(&the_condition_);
  sync_mutex_unlock(&mutex_);
}
