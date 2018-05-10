// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/vsock_endpoint.h"

#include "garnet/bin/guest/mgr/vsock_server.h"
#include "lib/fxl/logging.h"

namespace guestmgr {

VsockEndpoint::VsockEndpoint(uint32_t cid, VsockServer* server)
    : cid_(cid), vsock_server_(server) {}

VsockEndpoint::~VsockEndpoint() { vsock_server_->RemoveEndpoint(cid_); }

void VsockEndpoint::BindSocketEndpoint(guest::SocketEndpointPtr endpoint) {
  endpoint->SetContextId(cid_, connector_bindings_.AddBinding(this),
                         remote_acceptor_.NewRequest());
}

void VsockEndpoint::GetSocketConnector(
    fidl::InterfaceRequest<guest::SocketConnector> request) {
  connector_bindings_.AddBinding(this, std::move(request));
}

void VsockEndpoint::SetSocketAcceptor(
    fidl::InterfaceHandle<guest::SocketAcceptor> handle) {
  remote_acceptor_ = handle.Bind();
}

void VsockEndpoint::Connect(uint32_t src_port, uint32_t dest_cid,
                            uint32_t dest_port, ConnectCallback callback) {
  VsockEndpoint* dest = vsock_server_->FindEndpoint(dest_cid);
  if (dest == nullptr || !dest->remote_acceptor_) {
    callback(ZX_ERR_CONNECTION_REFUSED, zx::socket());
    return;
  }
  dest->remote_acceptor_->Accept(cid_, src_port, dest_port,
                                 std::move(callback));
}

}  //  namespace guestmgr
