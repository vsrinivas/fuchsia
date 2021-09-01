// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COMPONENT_LIFECYCLE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COMPONENT_LIFECYCLE_H_

#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include "coordinator.h"

namespace devmgr {
using SuspendCallback = fit::callback<void(zx_status_t)>;
class ComponentLifecycleServer final
    : public fidl::WireServer<fuchsia_process_lifecycle::Lifecycle> {
 public:
  explicit ComponentLifecycleServer(Coordinator* dev_coord, SuspendCallback callback)
      : dev_coord_(dev_coord), suspend_callback_(std::move(callback)) {}

  static zx_status_t Create(async_dispatcher_t* dispatcher, Coordinator* dev_coord,
                            fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> request,
                            SuspendCallback callback);

  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;

 private:
  Coordinator* dev_coord_;
  SuspendCallback suspend_callback_;
};

}  // namespace devmgr

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COMPONENT_LIFECYCLE_H_
