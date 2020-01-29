// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-device-thread.h"

namespace libdriver_integration_test {

MockDeviceThread::MockDeviceThread(fidl::InterfacePtr<Interface> interface)
    : interface_(std::move(interface)) {
  auto handler = [this](uint64_t action_id) { EventDone(action_id); };
  interface_.events().AddDeviceDone = handler;
  interface_.events().UnbindReplyDone = handler;
  interface_.events().SuspendReplyDone = handler;
  interface_.events().ResumeReplyDone = handler;
}

void MockDeviceThread::EventDone(uint64_t action_id) {
  // Check the list of pending actions and signal the corresponding completer
  auto itr = pending_actions_.find(action_id);
  ZX_ASSERT(itr != pending_actions_.end());
  itr->second.complete_ok();
  pending_actions_.erase(itr);
}

void MockDeviceThread::PerformActions(ActionList actions) {
  interface_->PerformActions(FinalizeActionList(std::move(actions)));
}

std::vector<ActionList::Action> MockDeviceThread::FinalizeActionList(ActionList action_list) {
  return action_list.FinalizeActionList(&pending_actions_, &next_action_id_);
}

}  // namespace libdriver_integration_test
