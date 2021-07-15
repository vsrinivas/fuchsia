// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/socat.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <zircon/status.h>

#include <iostream>

#include "lib/fidl/cpp/binding.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/virtualization/bin/guest/serial.h"
#include "src/virtualization/bin/guest/services.h"

namespace {

class HostVsockAcceptor : public fuchsia::virtualization::HostVsockAcceptor {
 public:
  HostVsockAcceptor(uint32_t port, async::Loop* loop) : port_(port), console_(loop) {}

  // |fuchsia::virtualization::HostVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    if (port != port_) {
      std::cerr << "Unexpected port " << port << "\n";
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    zx::socket socket, remote_socket;
    zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
    if (status != ZX_OK) {
      std::cerr << "Failed to create socket " << status << "\n";
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    callback(ZX_OK, std::move(remote_socket));
    console_.Start(zx::socket(std::move(socket)));
  }

 private:
  uint32_t port_;
  GuestConsole console_;
};

// Connect the the vsock endpoint of the given environment.
zx::status<fuchsia::virtualization::HostVsockEndpointSyncPtr> connect_to_vsock_endpoint(
    uint32_t env_id, uint32_t port, sys::ComponentContext* context) {
  // Connect to environment.
  zx::status<fuchsia::virtualization::RealmSyncPtr> realm = ConnectToEnvironment(context, env_id);
  if (realm.is_error()) {
    return realm.take_error();
  }

  fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
  zx_status_t status = realm->GetHostVsockEndpoint(vsock_endpoint.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Could not fetch vsock endpoint: " << zx_status_get_string(status) << ".\n";
    return zx::error(status);
  }

  return zx::ok(std::move(vsock_endpoint));
}

}  // namespace

zx_status_t handle_socat_listen(uint32_t env_id, uint32_t port, async::Loop* loop,
                                sys::ComponentContext* context) {
  zx::status<fuchsia::virtualization::HostVsockEndpointSyncPtr> vsock_endpoint =
      connect_to_vsock_endpoint(env_id, port, context);
  if (vsock_endpoint.is_error()) {
    return vsock_endpoint.error_value();
  }

  HostVsockAcceptor acceptor(port, loop);
  fidl::Binding<fuchsia::virtualization::HostVsockAcceptor> binding(&acceptor);
  zx_status_t status;
  vsock_endpoint->Listen(port, binding.NewBinding(), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to listen on port " << port << "\n";
    return status;
  }

  return loop->Run();
}

zx_status_t handle_socat_connect(uint32_t env_id, uint32_t cid, uint32_t port, async::Loop* loop,
                                 sys::ComponentContext* context) {
  zx::status<fuchsia::virtualization::HostVsockEndpointSyncPtr> vsock_endpoint =
      connect_to_vsock_endpoint(env_id, port, context);
  if (vsock_endpoint.is_error()) {
    return vsock_endpoint.error_value();
  }

  zx::socket socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    std::cerr << "Failed to create socket " << status << "\n";
    return status;
  }
  vsock_endpoint->Connect(cid, port, std::move(remote_socket), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to connect " << status << "\n";
    return status;
  }

  GuestConsole console(loop);
  console.Start(std::move(socket));

  return loop->Run();
}
