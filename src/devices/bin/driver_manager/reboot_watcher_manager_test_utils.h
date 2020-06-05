// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_REBOOT_WATCHER_MANAGER_TEST_UTILS_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_REBOOT_WATCHER_MANAGER_TEST_UTILS_H_

#include <fuchsia/hardware/power/statecontrol/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

class MockRebootMethodsWatcher
    : public llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcher::Interface {
 public:
  void OnReboot(llcpp::fuchsia::hardware::power::statecontrol::RebootReason reason,
                llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcher::Interface::
                    OnRebootCompleter::Sync completer) override {
    reason_ = reason;

    completer.Reply();
  }

  bool HasReason() const { return reason_.has_value(); }
  llcpp::fuchsia::hardware::power::statecontrol::RebootReason Reason() const {
    return reason_.value();
  }

 private:
  std::optional<llcpp::fuchsia::hardware::power::statecontrol::RebootReason> reason_;
};

// Delay replying to the server when a reboot reason is recieved.
class MockRebootMethodsWatcherDelaysReply
    : public llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcher::Interface {
 public:
  MockRebootMethodsWatcherDelaysReply(async_dispatcher_t* dispatcher, zx::duration delay)
      : dispatcher_(dispatcher), delay_(delay) {}

  void OnReboot(llcpp::fuchsia::hardware::power::statecontrol::RebootReason reason,
                llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcher::Interface::
                    OnRebootCompleter::Sync completer) override {
    reason_ = reason;

    // |completer|'s underlying transaction needs to be replied to or it will check-fail when it's
    // |completer| is destroyed.
    async::PostDelayedTask(
        dispatcher_, [completer = completer.ToAsync()]() mutable { completer.Reply(); }, delay_);
  }

  bool HasReason() const { return reason_.has_value(); }
  llcpp::fuchsia::hardware::power::statecontrol::RebootReason Reason() const {
    return reason_.value();
  }

 private:
  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
  std::optional<llcpp::fuchsia::hardware::power::statecontrol::RebootReason> reason_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_REBOOT_WATCHER_MANAGER_TEST_UTILS_H_
