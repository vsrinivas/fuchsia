// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "action-list.h"

#include <gtest/gtest.h>

#include "mock-device-thread.h"
#include "mock-device.h"

namespace libdriver_integration_test {

ActionList::ActionList() = default;
ActionList::~ActionList() = default;

void ActionList::AppendAction(Action action) { actions_.push_back(std::move(action)); }

void ActionList::AppendAddMockDevice(async_dispatcher_t* dispatcher, const std::string& parent_path,
                                     std::string name, std::vector<zx_device_prop_t> props,
                                     zx_status_t expect_status,
                                     std::unique_ptr<MockDevice>* new_device_out,
                                     fit::promise<void, std::string>* add_done_out) {
  fit::bridge<void, std::string> bridge;
  AppendAddMockDevice(dispatcher, parent_path, std::move(name), std::move(props), expect_status,
                      std::move(bridge.completer), new_device_out);
  *add_done_out = bridge.consumer.promise_or(::fit::error("add device abandoned")).box();
}

void ActionList::AppendAddMockDevice(async_dispatcher_t* dispatcher, const std::string& parent_path,
                                     std::string name, std::vector<zx_device_prop_t> props,
                                     zx_status_t expect_status,
                                     fit::completer<void, std::string> add_done,
                                     std::unique_ptr<MockDevice>* new_device_out) {
  fidl::InterfaceHandle<fuchsia::device::mock::MockDevice> client;
  fidl::InterfaceRequest<fuchsia::device::mock::MockDevice> server(client.NewRequest());
  if (!server.is_valid()) {
    EXPECT_TRUE(server.is_valid());
    return;
  }

  std::string path(parent_path + "/" + name);
  *new_device_out = std::make_unique<MockDevice>(std::move(server), dispatcher, std::move(path));

  fuchsia::device::mock::Action action;
  auto& add_device = action.add_device();
  add_device.do_bind = false;
  add_device.controller = std::move(client);
  add_device.name = std::move(name);
  add_device.expect_status = expect_status;

  static_assert(sizeof(uint64_t) == sizeof(props[0]));
  for (zx_device_prop_t prop : props) {
    add_device.properties.push_back(*reinterpret_cast<uint64_t*>(&prop));
  }

  add_device.action_id = next_action_id_++;

  local_action_map_[add_device.action_id] = std::move(add_done);
  return AppendAction(std::move(action));
}

void ActionList::AppendUnbindReply(fit::promise<void, std::string>* unbind_reply_done_out) {
  fit::bridge<void, std::string> bridge;
  AppendUnbindReply(std::move(bridge.completer));
  *unbind_reply_done_out = bridge.consumer.promise_or(::fit::error("unbind reply abandoned")).box();
}

void ActionList::AppendUnbindReply(fit::completer<void, std::string> unbind_reply_done) {
  Action action;
  auto& unbind_reply = action.unbind_reply();
  unbind_reply.action_id = next_action_id_++;

  fit::bridge<void, std::string> bridge;
  local_action_map_[unbind_reply.action_id] = std::move(unbind_reply_done);
  return AppendAction(std::move(action));
}

void ActionList::AppendAsyncRemoveDevice() {
  Action action;
  action.set_async_remove_device(true);
  return AppendAction(std::move(action));
}

void ActionList::AppendCreateThread(async_dispatcher_t* dispatcher,
                                    std::unique_ptr<MockDeviceThread>* out) {
  fidl::InterfacePtr<MockDeviceThread::Interface> interface;
  Action action;
  action.set_create_thread(interface.NewRequest(dispatcher));
  AppendAction(std::move(action));
  *out = std::make_unique<MockDeviceThread>(std::move(interface));
}

void ActionList::AppendReturnStatus(zx_status_t status) {
  Action action;
  action.set_return_status(status);
  return AppendAction(std::move(action));
}

// Consume this action list, updating the given |map| and action counter.
std::vector<ActionList::Action> ActionList::FinalizeActionList(CompleterMap* map,
                                                               uint64_t* next_action_id) {
  CompleterMap local_ids;
  for (auto& action : actions_) {
    uint64_t* action_id = nullptr;
    if (action.is_add_device()) {
      action_id = &action.add_device().action_id;
    } else if (action.is_unbind_reply()) {
      action_id = &action.unbind_reply().action_id;
    } else {
      continue;
    }
    uint64_t local_action_id = *action_id;
    auto itr = local_action_map_.find(local_action_id);
    ZX_ASSERT(itr != local_action_map_.end());
    uint64_t remote_action_id = (*next_action_id)++;
    (*map)[remote_action_id] = std::move(itr->second);
    local_action_map_.erase(itr);
    *action_id = remote_action_id;
  }
  return std::move(actions_);
}

}  // namespace libdriver_integration_test
