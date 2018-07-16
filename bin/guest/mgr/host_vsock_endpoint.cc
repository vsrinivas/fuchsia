// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"

#include <fbl/auto_call.h>
#include <lib/async/default.h>

#include "lib/fxl/logging.h"

namespace guestmgr {

HostVsockEndpoint::HostVsockEndpoint(uint32_t cid) : VsockEndpoint(cid) {}

HostVsockEndpoint::~HostVsockEndpoint() = default;

void HostVsockEndpoint::AddBinding(
    fidl::InterfaceRequest<fuchsia::guest::ManagedVsockEndpoint> request) {
  bindings_.AddBinding(this, std::move(request));
}

void HostVsockEndpoint::Accept(uint32_t src_cid, uint32_t src_port,
                               uint32_t port, AcceptCallback callback) {
  auto it = listeners_.find(port);
  if (it == listeners_.end()) {
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }
  it->second->Accept(src_cid, src_port, port, std::move(callback));
}

void HostVsockEndpoint::Listen(uint32_t port,
                               fidl::InterfaceHandle<VsockAcceptor> acceptor,
                               ListenCallback callback) {
  if (port_bitmap_.GetOne(port)) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  bool inserted;
  auto acceptor_ptr = acceptor.Bind();
  acceptor_ptr.set_error_handler([this, port]() {
    port_bitmap_.ClearOne(port);
    listeners_.erase(port);
  });
  std::tie(std::ignore, inserted) =
      listeners_.emplace(port, std::move(acceptor_ptr));
  if (!inserted) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  port_bitmap_.SetOne(port);
  callback(ZX_OK);
}

void HostVsockEndpoint::Connect(
    uint32_t cid, uint32_t port,
    fuchsia::guest::ManagedVsockEndpoint::ConnectCallback callback) {
  uint32_t src_port;
  zx_status_t status = FindEphemeralPort(&src_port);
  if (status != ZX_OK) {
    callback(ZX_ERR_NO_RESOURCES, zx::handle());
    return;
  }
  Connect(src_port, cid, port,
          [this, src_port, callback = std::move(callback)](
              zx_status_t status, zx::handle handle) mutable {
            ConnectCallback(status, std::move(handle), src_port,
                            std::move(callback));
          });
}

void HostVsockEndpoint::ConnectCallback(
    zx_status_t status, zx::handle handle, uint32_t src_port,
    fuchsia::guest::ManagedVsockEndpoint::ConnectCallback callback) {
  auto free_port =
      fbl::MakeAutoCall([this, src_port]() { FreePort(src_port); });
  if (status != ZX_OK) {
    callback(status, std::move(handle));
    return;
  }

  auto conn = std::make_unique<Connection>();
  conn->port = src_port;
  status = handle.duplicate(ZX_RIGHT_WAIT, &conn->handle);
  if (status != ZX_OK) {
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }
  conn->wait.set_trigger(__ZX_OBJECT_PEER_CLOSED);
  conn->wait.set_object(conn->handle.get());
  conn->wait.set_handler(
      [this, conn = conn.get()](...) { OnPeerClosed(conn); });
  status = conn->wait.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }

  free_port.cancel();
  connections_.emplace(conn->port, std::move(conn));
  callback(ZX_OK, std::move(handle));
}

void HostVsockEndpoint::OnPeerClosed(Connection* conn) {
  FreePort(conn->port);
  connections_.erase(conn->port);
}

zx_status_t HostVsockEndpoint::FindEphemeralPort(uint32_t* port) {
  size_t value;
  zx_status_t status = port_bitmap_.Find(false, kFirstEphemeralPort,
                                         kLastEphemeralPort, 1, &value);
  if (status != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }
  status = port_bitmap_.SetOne(value);
  *port = value;
  return ZX_OK;
}

zx_status_t HostVsockEndpoint::FreePort(uint32_t port) {
  return port_bitmap_.ClearOne(port);
}

}  // namespace guestmgr
