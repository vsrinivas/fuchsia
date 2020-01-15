// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SEMAPHORE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SEMAPHORE_H_

#include <stdint.h>
#include <zircon/types.h>

#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/wait.h>

// A basic counting semaphore. It directly uses the low-level wait queue API.
class Semaphore {
 public:
  explicit Semaphore(uint64_t initial_count = 0) : count_(initial_count) {}

  Semaphore(const Semaphore&) = delete;
  Semaphore(Semaphore&&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;
  ~Semaphore() = default;

  // Release a single thread if there are any waiting, otherwise increment the
  // internal count by one.
  void Post();

  // If the count is positive, decrement the count by exactly one.  Otherwise,
  // wait until some other thread wakes us, or our wait is interrupted by
  // timeout, suspend, or thread death.
  //
  // The return value can be ZX_ERR_TIMED_OUT if the deadline had passed or one
  // of ZX_ERR_INTERNAL_INTR errors if the thread had a signal delivered.
  zx_status_t Wait(const Deadline& deadline);

  // Observe the current internal count of the semaphore.
  uint64_t count() {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    return count_;
  }

  // Observe the current internal count of waiters
  uint64_t num_waiters() {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    return waitq_.Count();
  }

 private:
  uint64_t count_ TA_GUARDED(thread_lock);
  WaitQueue waitq_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SEMAPHORE_H_
