// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/event.h>

#ifndef SRC_BRINGUP_BIN_PWRBTN_MONITOR_OOM_WATCHER_H_
#define SRC_BRINGUP_BIN_PWRBTN_MONITOR_OOM_WATCHER_H_

namespace pwrbtn {
namespace statecontrol_fidl = fuchsia_hardware_power_statecontrol;

// This class is *not* thread safe and `WatchForOom` should not be called
// concurrently.
class OomWatcher {
 public:
  OomWatcher() = default;

  ~OomWatcher() = default;

  zx_status_t WatchForOom(async_dispatcher_t* dispatcher, zx::event oom_event,
                          fidl::ClientEnd<statecontrol_fidl::Admin> pwr_ctl);

 private:
  void OnOOM(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
             const zx_packet_signal_t* signal);
  async::WaitMethod<OomWatcher, &OomWatcher::OnOOM> wait_on_oom_event_{this};
  zx::event oom_event_;
  fidl::ClientEnd<statecontrol_fidl::Admin> pwr_ctl_;
};

}  // namespace pwrbtn

#endif  // SRC_BRINGUP_BIN_PWRBTN_MONITOR_OOM_WATCHER_H_
