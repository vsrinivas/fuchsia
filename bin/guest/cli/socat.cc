// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/socat.h"
#include <iostream>

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/guest/cli/serial.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/logging.h"

class GuestVsockAcceptor : public fuchsia::guest::HostVsockAcceptor {
 public:
  GuestVsockAcceptor(uint32_t port, async::Loop* loop)
      : port_(port), console_(loop) {}

  // |fuchsia::guest::GuestVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override {
    if (port != port_) {
      std::cerr << "Unexpected port " << port << "\n";
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    zx::socket socket, remote_socket;
    zx_status_t status =
        zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
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
  SerialConsole console_;
};

void handle_socat_listen(uint32_t env_id, uint32_t port) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  fuchsia::guest::GuestEnvironmentSyncPtr guest_env;
  guestmgr->ConnectToEnvironment(env_id, guest_env.NewRequest());
  fuchsia::guest::HostVsockEndpointSyncPtr vsock_endpoint;
  guest_env->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  GuestVsockAcceptor acceptor(port, &loop);
  fidl::Binding<fuchsia::guest::HostVsockAcceptor> binding(&acceptor);
  zx_status_t status;
  vsock_endpoint->Listen(port, binding.NewBinding(), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to listen on port " << port << "\n";
    return;
  }

  loop.Run();
}

void handle_socat_connect(uint32_t env_id, uint32_t cid, uint32_t port) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  fuchsia::guest::GuestEnvironmentSyncPtr guest_env;
  guestmgr->ConnectToEnvironment(env_id, guest_env.NewRequest());
  fuchsia::guest::HostVsockEndpointSyncPtr vsock_endpoint;
  guest_env->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  zx::socket socket, remote_socket;
  zx_status_t status =
      zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    std::cerr << "Failed to create socket " << status << "\n";
    return;
  }
  vsock_endpoint->Connect(cid, port, std::move(remote_socket), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to connect " << status << "\n";
    return;
  }

  SerialConsole console(&loop);
  console.Start(std::move(socket));
  loop.Run();
}
