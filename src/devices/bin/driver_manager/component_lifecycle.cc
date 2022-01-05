// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "component_lifecycle.h"

#include <lib/fidl-async/cpp/bind.h>
#include <zircon/status.h>

#include "fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h"
#include "src/devices/lib/log/log.h"

namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

namespace devmgr {

zx_status_t ComponentLifecycleServer::Create(
    async_dispatcher_t* dispatcher, Coordinator* dev_coord,
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> request, SuspendCallback callback) {
  zx_status_t status = fidl::BindSingleInFlightOnly(
      dispatcher, std::move(request),
      std::make_unique<ComponentLifecycleServer>(dev_coord, std::move(callback)));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to bind component lifecycle service:%s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

void ComponentLifecycleServer::Stop(StopRequestView request, StopCompleter::Sync& completer) {
  LOGF(INFO, "Received component lifecycle stop event");
  dev_coord_->suspend_resume_manager()->Suspend(
      dev_coord_->suspend_resume_manager()->GetSuspendFlagsFromSystemPowerState(
          dev_coord_->shutdown_system_state()),
      std::move(suspend_callback_));
}
}  // namespace devmgr
