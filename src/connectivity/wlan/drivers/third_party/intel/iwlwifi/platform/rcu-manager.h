// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_RCU_MANAGER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_RCU_MANAGER_H_

#include <lib/async/dispatcher.h>
#include <zircon/types.h>

#include <shared_mutex>

namespace wlan::iwlwifi {

// This class manages RCU-based synchronization for a set of threads.
class RcuManager {
 public:
  explicit RcuManager(async_dispatcher_t* dispatcher);
  ~RcuManager();

  // Initialize the RCU manager for the current thread.  This must be run on every thread that will
  // be using RCU.
  void InitForThread();

  // Enter a RCU read-side lock.
  void ReadLock();

  // Exit a RCU read-side lock.
  void ReadUnlock();

  // Wait for all existing read-side locks to unlock.
  void Sync();

  // Helper: call a function after synchronizing.
  void CallSync(void (*func)(void*), void* data);

  // Helper: free() an allocation after synchronizing.
  void FreeSync(void* alloc);

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::shared_mutex rwlock_;
  static thread_local int read_lock_count_;
  zx_futex_t call_count_ = 0;
};

}  // namespace wlan::iwlwifi

// This subclass-as-an-alias exists purely to be compatible with C code that uses the
// `rcu_manager` type as a struct pointer.
struct rcu_manager final : public wlan::iwlwifi::RcuManager {};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_RCU_MANAGER_H_
