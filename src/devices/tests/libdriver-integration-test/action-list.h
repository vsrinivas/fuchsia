// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_ACTION_LIST_H_
#define SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_ACTION_LIST_H_

#include <fuchsia/device/mock/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include <string>
#include <vector>

namespace libdriver_integration_test {

// Forward-declarations
class MockDevice;
class MockDeviceThread;

// Represents an ordered list of actions to perform.
class ActionList {
 public:
  using Action = fuchsia::device::mock::Action;
  using CompleterMap = std::map<uint64_t, fit::completer<void, std::string>>;

  ActionList();
  ~ActionList();

  ActionList(ActionList&&) = default;
  ActionList& operator=(ActionList&&) = default;

  ActionList(const ActionList&) = delete;
  ActionList& operator=(const ActionList&) = delete;

  const std::vector<Action>& actions() const { return actions_; }

  // Methods for adding to the stored actions
  void AppendAction(Action action);
  void AppendAddMockDevice(async_dispatcher_t* dispatcher, const std::string& parent_path,
                           std::string name, std::vector<zx_device_prop_t> props,
                           zx_status_t expect_status, std::unique_ptr<MockDevice>* new_device_out,
                           fit::promise<void, std::string>* add_done_out);
  void AppendAddMockDevice(async_dispatcher_t* dispatcher, const std::string& parent_path,
                           std::string name, std::vector<zx_device_prop_t> props,
                           zx_status_t expect_status, fit::completer<void, std::string> add_done,
                           std::unique_ptr<MockDevice>* new_device_out);
  void AppendUnbindReply(fit::promise<void, std::string>* unbind_reply_done_out);
  void AppendUnbindReply(fit::completer<void, std::string> unbind_reply_done);
  void AppendAsyncRemoveDevice();
  void AppendCreateThread(async_dispatcher_t* dispatcher, std::unique_ptr<MockDeviceThread>* out);
  void AppendReturnStatus(zx_status_t status);

  // Consume this action list, updating the given |map| and action counter.
  std::vector<Action> FinalizeActionList(CompleterMap* map, uint64_t* next_action_id);

 private:
  std::vector<Action> actions_;

  // Map of locally assigned action IDs to completers for them.  These will be
  // remapped by MockDevice::FinalizeActionList.
  CompleterMap local_action_map_;
  uint64_t next_action_id_ = 0;
};

}  // namespace libdriver_integration_test

#endif  // SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_ACTION_LIST_H_
