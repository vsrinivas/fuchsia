// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_REMOVAL_TRACKER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_REMOVAL_TRACKER_H_

#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <list>
#include <map>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "src/devices/bin/driver_manager/v2/node.h"

namespace dfv2 {
class NodeRemovalTracker {
 public:
  void RegisterNode(void* node_ptr, Collection node_collection, std::string name, NodeState state);
  void NotifyNoChildren(void* node_ptr);
  void NotifyRemovalComplete(void* node_ptr);
  void set_pkg_callback(fit::callback<void()> callback);
  void set_all_callback(fit::callback<void()> callback);

 private:
  std::map<void*, std::tuple<std::string, Collection, NodeState>> nodes_;
  fbl::Mutex callback_lock_;
  fit::callback<void()> pkg_callback_ __TA_GUARDED(callback_lock_);
  fit::callback<void()> all_callback_ __TA_GUARDED(callback_lock_);
};

}  // namespace dfv2
#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_REMOVAL_TRACKER_H_
