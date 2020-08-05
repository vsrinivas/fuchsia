// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "component_lifecycle.h"

#include <lib/fidl-async/cpp/bind.h>
#include <zircon/status.h>

#include "fuchsia/hardware/power/statecontrol/llcpp/fidl.h"
#include "src/devices/lib/log/log.h"

namespace power_fidl = llcpp::fuchsia::hardware::power;

namespace devmgr {

zx_status_t ComponentLifecycleServer::Create(async::Loop* loop, Coordinator* dev_coord,
                                             zx::channel chan) {
  if (!loop) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status =
      fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(chan),
                                   std::make_unique<ComponentLifecycleServer>(dev_coord, loop));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to bind component lifecycle service:%s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

void ComponentLifecycleServer::Stop(StopCompleter::Sync completer) {
  auto callback = [this, completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      LOGF(ERROR, "Error suspending devices while stopping the component:%s",
           zx_status_get_string(status));
    }
    LOGF(INFO, "Exiting driver manager gracefully");
    loop_->Quit();
  };

  dev_coord_->Suspend(SuspendContext(SuspendContext::Flags::kSuspend,
                                     dev_coord_->GetSuspendFlagsFromSystemPowerState(
                                         dev_coord_->shutdown_system_state())),
                      std::move(callback));
}
}  // namespace devmgr
