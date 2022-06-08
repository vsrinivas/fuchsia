// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_PISTRESS_SYNC_OBJ_H_
#define SRC_ZIRCON_BIN_PISTRESS_SYNC_OBJ_H_

#include <lib/sync/condition.h>
#include <lib/sync/mutex.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/compiler.h>

#include <optional>

#include "behavior.h"

// SyncObj is the base class which defines the interface for synchronization
// objects (which behave like mutexes) used during profile inheritance stress
// testing.  During testing, threads will acquire and release chains of these
// synchronization objects, lingering inside of them for randomized periods of
// time.  The purpose is to create a large number of profile inheritance events
// which exercise as many difference scenarios as possible.
class __TA_CAPABILITY("mutex") SyncObj {
 public:
  SyncObj() = default;
  virtual ~SyncObj() = default;
  virtual void Acquire(TestThreadBehavior& behavior) __TA_ACQUIRE() = 0;
  virtual void Release() __TA_RELEASE() = 0;
  virtual void Shutdown() {}

 protected:
  static zx::time GetAcqTimeout(TestThreadBehavior& behavior);
};

// MutexSyncObj is a very simple implementation of SyncObj which just uses a
// futex based mutex.  This should provide a lot of coverage for the
// wait-and-assign-owner and wake-owner futex operations.
class MutexSyncObj : public SyncObj {
 public:
  MutexSyncObj() = default;
  ~MutexSyncObj() override = default;

  // TODO(johngro) : Add AssertHeld to sync_mutex_t so we can make the static
  // analyzer happy when we attempt to acquire with a timeout.
  void Acquire(TestThreadBehavior& behavior) override __TA_ACQUIRE(mutex_);
  void Release() override __TA_RELEASE(mutex_);

 private:
  sync_mutex_t mutex_;
};

// CondVarSyncObj is pretty much just an implementation of a mutex, but using a
// condvar.  In real life, you would never actually want to do this (using an
// actual mutex based on a futex would be much better), but it allows us to
// exercise the wake and re-queue futex operation; something which typically
// sees little coverage (since condvars do not seem to be used nearly as much as
// other sync primitives in the system).
class CondVarSyncObj : public SyncObj {
 public:
  CondVarSyncObj() = default;
  ~CondVarSyncObj() override = default;

  void Acquire(TestThreadBehavior& behavior) override;
  void Release() override;
  void Shutdown() override;

 private:
  sync_mutex_t mutex_;
  sync_condition_t the_condition_;
  std::optional<thrd_t> owner_ __TA_GUARDED(mutex_);
  bool shutdown_now_ __TA_GUARDED(mutex_) = false;
};

#endif  // SRC_ZIRCON_BIN_PISTRESS_SYNC_OBJ_H_
