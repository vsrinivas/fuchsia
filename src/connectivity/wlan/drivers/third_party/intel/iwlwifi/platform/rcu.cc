// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu.h"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu-manager.h"

void iwl_rcu_read_lock(struct device* dev) { dev->rcu_manager->ReadLock(); }

void iwl_rcu_read_unlock(struct device* dev) { dev->rcu_manager->ReadUnlock(); }

void iwl_rcu_sync(struct device* dev) { dev->rcu_manager->Sync(); }

void iwl_rcu_call_sync(struct device* dev, iwl_rcu_call_func func, void* data) {
  dev->rcu_manager->CallSync(func, data);
}

void iwl_rcu_free_sync(struct device* dev, void* alloc) { dev->rcu_manager->FreeSync(alloc); }
