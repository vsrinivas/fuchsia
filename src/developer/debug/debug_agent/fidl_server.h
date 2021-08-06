// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_FIDL_SERVER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_FIDL_SERVER_H_

#include <fuchsia/debugger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/socket_connection.h"

namespace debug_agent {

class DebugAgentImpl : public fuchsia::debugger::DebugAgent {
 public:
  explicit DebugAgentImpl(debug_agent::DebugAgent* agent) : debug_agent_(agent) {}
  void Connect(zx::socket socket, ConnectCallback callback) override;

 private:
  bool has_connection_ = false;
  std::unique_ptr<debug_agent::RemoteAPIAdapter> adapter_;
  std::unique_ptr<debug::BufferedZxSocket> buffer_;
  debug_agent::DebugAgent* debug_agent_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_FIDL_SERVER_H_
