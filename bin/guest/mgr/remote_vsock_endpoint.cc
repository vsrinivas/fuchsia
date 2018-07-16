// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/remote_vsock_endpoint.h"

namespace guestmgr {

RemoteVsockEndpoint::RemoteVsockEndpoint(uint32_t cid) : VsockEndpoint(cid) {}

RemoteVsockEndpoint::~RemoteVsockEndpoint() = default;

void RemoteVsockEndpoint::BindVsockEndpoint(
    fuchsia::guest::VsockEndpointPtr endpoint) {
  endpoint->SetContextId(cid(), connector_bindings_.AddBinding(this),
                         remote_acceptor_.NewRequest());
}

void RemoteVsockEndpoint::GetVsockConnector(
    fidl::InterfaceRequest<fuchsia::guest::VsockConnector> request) {
  connector_bindings_.AddBinding(this, std::move(request));
}

void RemoteVsockEndpoint::SetVsockAcceptor(
    fidl::InterfaceHandle<fuchsia::guest::VsockAcceptor> handle) {
  remote_acceptor_ = handle.Bind();
}

void RemoteVsockEndpoint::Accept(uint32_t src_cid, uint32_t src_port,
                                 uint32_t port, AcceptCallback callback) {
  if (!remote_acceptor_) {
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }
  remote_acceptor_->Accept(src_cid, src_port, port, std::move(callback));
}

}  //  namespace guestmgr
