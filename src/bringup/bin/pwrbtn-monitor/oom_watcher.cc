// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/pwrbtn-monitor/oom_watcher.h"

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <zircon/status.h>

namespace pwrbtn {
namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

zx_status_t OomWatcher::WatchForOom(async_dispatcher_t* dispatcher, zx::event oom_event,
                                    fidl::ClientEnd<statecontrol_fidl::Admin> pwr_ctl) {
  this->oom_event_ = std::move(oom_event);
  this->pwr_ctl_ = std::move(pwr_ctl);
  wait_on_oom_event_.set_object(oom_event_.release());
  wait_on_oom_event_.set_trigger(ZX_EVENT_SIGNALED);
  return wait_on_oom_event_.Begin(dispatcher);
}

void OomWatcher::OnOOM(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
  printf("pwrbtn-monitor: received kernel OOM signal\n");
  fidl::WireSyncClient sync_client{std::move(pwr_ctl_)};
  fidl::WireResult r_status = sync_client->Reboot(statecontrol_fidl::RebootReason::kOutOfMemory);
  if (r_status.status() || r_status->is_error()) {
    printf("pwrbtn-monitor: got error trying reboot: %s\n", r_status.FormatDescription().c_str());
  }
}
}  // namespace pwrbtn
