// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_MOCK_DEVICE_THREAD_H_
#define SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_MOCK_DEVICE_THREAD_H_

#include <fuchsia/device/mock/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>

#include <memory>
#include <utility>

#include "action-list.h"
#include "mock-device-hooks.h"

namespace libdriver_integration_test {

class MockDeviceThread {
 public:
  using Interface = fuchsia::device::mock::MockDeviceThread;

  explicit MockDeviceThread(fidl::InterfacePtr<Interface> interface);

  void PerformActions(ActionList actions);

 private:
  // Walks the action list and patches up any action_ids before converting it
  // to a vector
  std::vector<ActionList::Action> FinalizeActionList(ActionList actions);

  // Callback invoked whenever a pending action event completion comes in.
  // This handles completing the relevant completer.
  void EventDone(uint64_t action_id);

  fidl::InterfacePtr<Interface> interface_;

  // Completers for pending add/remove actions, so we can signal when the
  // operations are finished.
  std::map<uint64_t, fpromise::completer<void, std::string>> pending_actions_;
  uint64_t next_action_id_ = 0;
};

}  // namespace libdriver_integration_test

#endif  // SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_MOCK_DEVICE_THREAD_H_
