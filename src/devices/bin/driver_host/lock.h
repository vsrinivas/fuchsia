// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_LOCK_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_LOCK_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <atomic>

// locking and lock debugging

class TA_CAP("mutex") ApiLock {
 public:
  void Acquire() TA_ACQ() {
    lock_.Acquire();
    api_lock_owner_.store(thrd_current());
  }
  void Release() TA_REL() {
    api_lock_owner_.store(0);
    lock_.Release();
  }

  bool IsHeldByCurrentThread() { return thrd_equal(api_lock_owner_.load(), thrd_current()); }

 private:
  fbl::Mutex lock_;
  std::atomic<thrd_t> api_lock_owner_ = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_LOCK_H_
