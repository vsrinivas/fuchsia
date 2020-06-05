// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_REBOOT_WATCHER_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_REBOOT_WATCHER_MANAGER_H_

#include <fuchsia/hardware/power/statecontrol/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/channel.h>

#include <optional>

// TODO(fxb/52901): Delete this once power_manager serves |RebootMethodsWatcherRegister|.
class RebootWatcherManager : public llcpp::fuchsia::hardware::power::statecontrol::
                                 RebootMethodsWatcherRegister::Interface {
 public:
  RebootWatcherManager(async_dispatcher_t* dispatcher);

  // RebootWatcherRegister interface
  void Register(zx::channel watcher,
                llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister::
                    Interface::RegisterCompleter::Sync _completer) override;

  // Sets the reboot reason. If the reason has already been set, this function does nothing.
  void SetRebootReason(llcpp::fuchsia::hardware::power::statecontrol::RebootReason reason);

  size_t NumWatchers() const;

  // Returns true if |RebootWatcherManager| has at least 1 bound watcher and |reason_| is
  // set.
  bool ShouldNotifyWatchers() const;

  // Notify all of the watchers of the current suspend request. Execute |watchdog| and close all
  // open channels if all of the watchers fail to respond or excute |on_last_reply| when the last
  // watcher has responded, whichever occurs first.
  void NotifyAll(
      fit::closure watchdog, fit::closure on_last_reply = [] {});

  // If |watchdog_task_| is still pending, cancel it post it to execute immediately.
  void ExecuteWatchdog();

  bool HasRebootReason() const { return reason_.has_value(); }

 private:
  void UnbindWatcher(size_t idx);

  struct Watcher {
    fidl::Client<llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> client;

    // Track if the client is bound to avoid sending messages to unbound |RebootMethodsWatchers|.
    bool client_is_bound;
  };

  async_dispatcher_t* dispatcher_;

  std::optional<llcpp::fuchsia::hardware::power::statecontrol::RebootReason> reason_;

  std::vector<Watcher> watchers_;
  size_t num_bound_watchers_ = 0;

  std::unique_ptr<async::TaskClosure> watchdog_task_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_REBOOT_WATCHER_MANAGER_H_
