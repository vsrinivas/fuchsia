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
                RegisterCompleter::Sync& completer) override {
    sync_completion_signal(&register_called_);
    completer.ReplySuccess();
  }

  void wait_until_register_called() {
    sync_completion_wait(&register_called_, ZX_TIME_INFINITE);
    sync_completion_reset(&register_called_);
  }

 private:
  sync_completion_t register_called_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_MOCK_POWER_MANAGER_H_
