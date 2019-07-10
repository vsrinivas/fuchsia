// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AGENT_CONFIGURATION_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AGENT_CONFIGURATION_H_

#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

// Meant to handle all the configurations values that can be changed
// programatically by the client.
struct AgentConfiguration {
  bool quit_on_exit = false;
};

// Receives a list of actions and resolves them. Returns a status for each
// action received, in the same order.
std::vector<debug_ipc::zx_status_t> HandleActions(
    const std::vector<debug_ipc::ConfigAction>& actions, AgentConfiguration* config);

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AGENT_CONFIGURATION_H_
