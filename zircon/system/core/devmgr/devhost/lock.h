// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_LOCK_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_LOCK_H_

#include <threads.h>
#include <zircon/thread_annotations.h>

#include <atomic>

namespace devmgr {

// locking and lock debugging

namespace internal {
extern mtx_t devhost_api_lock;
extern std::atomic<thrd_t> devhost_api_lock_owner;
}  // namespace internal

#define REQ_DM_LOCK TA_REQ(&::devmgr::internal::devhost_api_lock)
#define USE_DM_LOCK TA_GUARDED(&::devmgr::internal::devhost_api_lock)

static inline void DM_LOCK() TA_ACQ(&::devmgr::internal::devhost_api_lock) {
  mtx_lock(&internal::devhost_api_lock);
  internal::devhost_api_lock_owner.store(thrd_current());
}

static inline void DM_UNLOCK() TA_REL(&::devmgr::internal::devhost_api_lock) {
  internal::devhost_api_lock_owner.store(0);
  mtx_unlock(&internal::devhost_api_lock);
}

static inline bool DM_LOCK_HELD() {
  return thrd_equal(internal::devhost_api_lock_owner.load(), thrd_current());
}

class ApiAutoLock {
 public:
  ApiAutoLock() TA_ACQ(&::devmgr::internal::devhost_api_lock) { DM_LOCK(); }
  ~ApiAutoLock() TA_REL(&::devmgr::internal::devhost_api_lock) { DM_UNLOCK(); }
};

class ApiAutoRelock {
 public:
  ApiAutoRelock() TA_REL(&::devmgr::internal::devhost_api_lock) { DM_UNLOCK(); }
  ~ApiAutoRelock() TA_ACQ(&::devmgr::internal::devhost_api_lock) { DM_LOCK(); }
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVHOST_LOCK_H_
