// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_RCU_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_RCU_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

struct device;

typedef void (*iwl_rcu_call_func)(void*);

// Enter a RCU read-side lock.  May be nested.
void iwl_rcu_read_lock(struct device* dev);

// Exit a RCU read-side lock.  May be nested.
void iwl_rcu_read_unlock(struct device* dev);

// Wait for all existing read-side locks to unlock.
void iwl_rcu_sync(struct device* dev);

// Call a function after synchronizing with existing read-side locks.
void iwl_rcu_call_sync(struct device* dev, iwl_rcu_call_func func, void* data);

// Free an allocation after synchronizing with existing read-side locks.
void iwl_rcu_free_sync(struct device* dev, void* alloc);

#define iwl_rcu_load(p) (atomic_load_explicit((_Atomic(__typeof(p))*)&p, memory_order_acquire))
#define iwl_rcu_store(p, v) \
  (atomic_store_explicit((_Atomic(__typeof(p))*)&p, v, memory_order_release))
#define iwl_rcu_exchange(p, v) \
  (atomic_exchange_explicit((_Atomic(__typeof(p))*)&p, v, memory_order_acq_rel))

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_RCU_H_
