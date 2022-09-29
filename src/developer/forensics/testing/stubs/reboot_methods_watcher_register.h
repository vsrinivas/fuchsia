// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_REBOOT_METHODS_WATCHER_REGISTER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_REBOOT_METHODS_WATCHER_REGISTER_H_

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>

#include <utility>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using RebootMethodsWatcherRegisterBase = SINGLE_BINDING_STUB_FIDL_SERVER(
    fuchsia::hardware::power::statecontrol, RebootMethodsWatcherRegister);

// A RebootMethodsWatcherRegister that binds then immediately sends the provided `reason` to the
// watcher.
class RebootMethodsWatcherRegister : public RebootMethodsWatcherRegisterBase {
 public:
  RebootMethodsWatcherRegister(fuchsia::hardware::power::statecontrol::RebootReason reason)
      : reason_(reason) {}

  // |fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister|.
  void Register(
      ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher)
      override;
  void RegisterWithAck(
      ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher,
      RegisterWithAckCallback callback) override;

 private:
  fuchsia::hardware::power::statecontrol::RebootReason reason_;
  fuchsia::hardware::power::statecontrol::RebootMethodsWatcherPtr watcher_;
};

// A RebootMethodsWatcherRegister that binds and then does nothing.
class RebootMethodsWatcherRegisterHangs : public RebootMethodsWatcherRegisterBase {
 public:
  bool IsBound() const override { return watcher_.is_bound(); }

  // |fuchsia::hardware::power::statecontrol::RebootMethodsWatcherRegister|.
  void Register(
      ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher)
      override;
  void RegisterWithAck(
      ::fidl::InterfaceHandle<fuchsia::hardware::power::statecontrol::RebootMethodsWatcher> watcher,
      RegisterWithAckCallback callback) override;

 private:
  fuchsia::hardware::power::statecontrol::RebootMethodsWatcherPtr watcher_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_REBOOT_METHODS_WATCHER_REGISTER_H_
