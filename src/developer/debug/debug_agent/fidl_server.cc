// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/fidl_server.h"

#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

void DebugAgentImpl::Connect(zx::socket socket, ConnectCallback callback) {
  if (has_connection_) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }

  zx_status_t status = buffer_.Init(std::move(socket));
  if (status != ZX_OK) {
    callback(status);
    return;
  }
  // Route data from the router_buffer -> RemoteAPIAdapter -> DebugAgent.
  adapter_ = std::make_unique<debug_agent::RemoteAPIAdapter>(debug_agent_, &buffer_.stream());
  buffer_.set_data_available_callback(
      [adapter = adapter_.get()]() { adapter->OnStreamReadable(); });

  // Exit the message loop on error.
  buffer_.set_error_callback([]() {
    DEBUG_LOG(Agent) << "Remote socket connection lost";
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Connect the buffer into the agent.
  debug_agent_->Connect(&buffer_.stream());
  status = buffer_.Start();
  if (status != ZX_OK) {
    callback(status);
    return;
  }

  DEBUG_LOG(Agent) << "Remote agent connected to debug_agent";

  has_connection_ = true;
  callback(ZX_OK);
}

}  // namespace debug_agent
