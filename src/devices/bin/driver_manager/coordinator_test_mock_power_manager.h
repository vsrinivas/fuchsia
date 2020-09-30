// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_MOCK_POWER_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_MOCK_POWER_MANAGER_H_

#include <fuchsia/power/manager/llcpp/fidl.h>
#include <lib/zx/channel.h>

class MockPowerManager
    : public llcpp::fuchsia::power::manager::DriverManagerRegistration::Interface {
 public:
  void Register(zx::channel system_state_transition, zx::channel dir,
                RegisterCompleter::Sync completer) override {
    register_called_ = true;
    completer.ReplySuccess();
  }

  bool register_called() { return register_called_; }

 private:
  bool register_called_ = false;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_MOCK_POWER_MANAGER_H_
