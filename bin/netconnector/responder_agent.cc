// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/responder_agent.h"

#include "apps/netconnector/src/netconnector_impl.h"
#include "lib/ftl/logging.h"

namespace netconnector {

// static
std::unique_ptr<ResponderAgent> ResponderAgent::Create(
    ftl::UniqueFD socket_fd,
    NetConnectorImpl* owner) {
  return std::unique_ptr<ResponderAgent>(
      new ResponderAgent(std::move(socket_fd), owner));
}

ResponderAgent::ResponderAgent(ftl::UniqueFD socket_fd, NetConnectorImpl* owner)
    : MessageTransciever(std::move(socket_fd)), owner_(owner) {}

ResponderAgent::~ResponderAgent() {}

void ResponderAgent::OnVersionReceived(uint32_t version) {}

void ResponderAgent::OnResponderNameReceived(std::string responder_name) {
  ResponderPtr responder = owner_->GetResponder(responder_name);
  if (!responder) {
    // Responder name not recognized. GetResponder logs a warning, so we
    // don't have to.
    CloseConnection();
    return;
  }

  responder.set_connection_error_handler([this]() { CloseConnection(); });

  mx::channel local;
  mx::channel remote;
  mx_status_t status = mx::channel::create(0u, &local, &remote);

  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create channel, status " << status;
    CloseConnection();
    return;
  }

  SetChannel(std::move(local));

  responder->ConnectionRequested(responder_name, std::move(remote));
}

void ResponderAgent::OnConnectionClosed() {
  FTL_DCHECK(owner_ != nullptr);
  owner_->ReleaseResponderAgent(this);
}

}  // namespace netconnector
