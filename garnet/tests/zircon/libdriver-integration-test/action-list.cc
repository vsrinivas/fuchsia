// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "action-list.h"

#include <gtest/gtest.h>

#include "mock-device.h"

namespace libdriver_integration_test {

ActionList::ActionList() = default;
ActionList::~ActionList() = default;

void ActionList::AppendAction(Action action) {
    actions_.push_back(std::move(action));
}

void ActionList::AppendAddMockDevice(async_dispatcher_t* dispatcher,
                                     const std::string& parent_path, std::string name,
                                     std::unique_ptr<MockDevice>* new_device_out,
                                     fit::promise<void, std::string>* add_done_out) {
    fit::bridge<void, std::string> bridge;
    AppendAddMockDevice(dispatcher, parent_path, name, std::move(bridge.completer), new_device_out);
    *add_done_out = bridge.consumer.promise_or(::fit::error("add device abandoned")).box();
}

void ActionList::AppendAddMockDevice(async_dispatcher_t* dispatcher,
                                     const std::string& parent_path, std::string name,
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
    add_device.action_id = next_action_id_++;

    local_action_map_[add_device.action_id] = std::move(add_done);
    return AppendAction(std::move(action));
}

void ActionList::AppendRemoveDevice(fit::promise<void, std::string>* remove_done_out) {
    fit::bridge<void, std::string> bridge;
    AppendRemoveDevice(std::move(bridge.completer));
    *remove_done_out = bridge.consumer.promise_or(::fit::error("remove device abandoned")).box();
}

void ActionList::AppendRemoveDevice(fit::completer<void, std::string> remove_done) {
    Action action;
    auto& remove_device = action.remove_device();
    remove_device.action_id = next_action_id_++;

    fit::bridge<void, std::string> bridge;
    local_action_map_[remove_device.action_id] =  std::move(remove_done);
    return AppendAction(std::move(action));
}

void ActionList::AppendReturnStatus(zx_status_t status) {
    Action action;
    action.set_return_status(status);
    return AppendAction(std::move(action));
}

void ActionList::Take(std::vector<Action>* actions,
                      std::map<uint64_t, fit::completer<void, std::string>>* local_action_map) {
    *actions = std::move(actions_);
    *local_action_map = std::move(local_action_map_);
}

} // namespace libdriver_integration_test

