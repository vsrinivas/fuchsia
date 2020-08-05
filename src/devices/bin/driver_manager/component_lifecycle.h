// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COMPONENT_LIFECYCLE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COMPONENT_LIFECYCLE_H_

#include <fuchsia/process/lifecycle/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include "coordinator.h"

namespace devmgr {

class ComponentLifecycleServer final
    : public llcpp::fuchsia::process::lifecycle::Lifecycle::Interface {
 public:
  explicit ComponentLifecycleServer(Coordinator* dev_coord, async::Loop* loop)
      : dev_coord_(dev_coord), loop_(loop) {}

  static zx_status_t Create(async::Loop* loop, Coordinator* dev_coord, zx::channel chan);

  void Stop(StopCompleter::Sync completer) override;

 private:
  Coordinator* dev_coord_;
  async::Loop* loop_;
};

}  // namespace devmgr

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COMPONENT_LIFECYCLE_H_
