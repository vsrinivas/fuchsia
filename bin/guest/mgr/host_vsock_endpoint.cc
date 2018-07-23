// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"

#include <fbl/auto_call.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>

namespace guestmgr {

HostVsockEndpoint::HostVsockEndpoint(AcceptorProvider acceptor_provider)
    : acceptor_provider_(std::move(acceptor_provider)) {}

void HostVsockEndpoint::AddBinding(
    fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> request) {
  bindings_.AddBinding(this, std::move(request));
}

void HostVsockEndpoint::Connect(
    uint32_t src_cid, uint32_t src_port, uint32_t cid, uint32_t port,
    fuchsia::guest::HostVsockConnector::ConnectCallback callback) {
  if (cid == fuchsia::guest::kHostCid) {
    // Guest to host connection.
    auto it = listeners_.find(port);
    if (it == listeners_.end()) {
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    it->second->Accept(src_cid, src_port, port, std::move(callback));
  } else {
    // Guest to guest connection.
    fuchsia::guest::GuestVsockAcceptor* acceptor = acceptor_provider_(cid);
    if (acceptor == nullptr) {
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    // Use a socket for direct guest to guest communication.
    zx::socket h1, h2;
    zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2);
    if (status != ZX_OK) {
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    acceptor->Accept(
        src_cid, src_port, port, std::move(h1),
        [callback = std::move(callback), h2 = std::move(h2)](
            zx_status_t status) mutable { callback(status, std::move(h2)); });
  }
}

void HostVsockEndpoint::Listen(
    uint32_t port,
    fidl::InterfaceHandle<fuchsia::guest::HostVsockAcceptor> acceptor,
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
    uint32_t cid, uint32_t port, zx::handle handle,
    fuchsia::guest::HostVsockEndpoint::ConnectCallback callback) {
  if (cid == fuchsia::guest::kHostCid) {
    FXL_LOG(ERROR) << "Attempt to connect to host service from host";
    callback(ZX_ERR_CONNECTION_REFUSED);
    return;
  }
  fuchsia::guest::GuestVsockAcceptor* acceptor = acceptor_provider_(cid);
  if (acceptor == nullptr) {
    callback(ZX_ERR_CONNECTION_REFUSED);
    return;
  }
  zx::handle dup;
  zx_status_t status = handle.duplicate(ZX_RIGHT_WAIT, &dup);
  if (status != ZX_OK) {
    callback(status);
    return;
  }
  uint32_t src_port;
  status = AllocEphemeralPort(&src_port);
  if (status != ZX_OK) {
    callback(status);
    return;
  }
  // Get access to the guests.
  acceptor->Accept(
      fuchsia::guest::kHostCid, src_port, port, std::move(handle),
      [this, dup = std::move(dup), src_port,
       callback = std::move(callback)](zx_status_t status) mutable {
        ConnectCallback(status, std::move(dup), src_port, std::move(callback));
      });
}

void HostVsockEndpoint::ConnectCallback(
    zx_status_t status, zx::handle dup, uint32_t src_port,
    fuchsia::guest::HostVsockEndpoint::ConnectCallback callback) {
  auto free_port =
      fbl::MakeAutoCall([this, src_port]() { FreeEphemeralPort(src_port); });
  if (status != ZX_OK) {
    callback(status);
    return;
  }

  auto conn = std::make_unique<Connection>();
  conn->port = src_port;
  conn->handle = std::move(dup);
  conn->wait.set_trigger(__ZX_OBJECT_PEER_CLOSED);
  conn->wait.set_object(conn->handle.get());
  conn->wait.set_handler(
      [this, conn = conn.get()](...) { OnPeerClosed(conn); });
  status = conn->wait.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    callback(status);
    return;
  }

  connections_.emplace(conn->port, std::move(conn));
  free_port.cancel();
  callback(ZX_OK);
}

void HostVsockEndpoint::OnPeerClosed(Connection* conn) {
  FreeEphemeralPort(conn->port);
  connections_.erase(conn->port);
}

zx_status_t HostVsockEndpoint::AllocEphemeralPort(uint32_t* port) {
  size_t value;
  zx_status_t status = port_bitmap_.Find(false, kFirstEphemeralPort,
                                         kLastEphemeralPort, 1, &value);
  if (status != ZX_OK) {
    return ZX_ERR_NO_RESOURCES;
  }
  *port = value;
  return port_bitmap_.SetOne(value);
}

void HostVsockEndpoint::FreeEphemeralPort(uint32_t port) {
  __UNUSED zx_status_t status = port_bitmap_.ClearOne(port);
  FXL_DCHECK(status == ZX_OK);
}

}  // namespace guestmgr
