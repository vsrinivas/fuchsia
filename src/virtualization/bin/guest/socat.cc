// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/socat.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <iostream>

#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/virtualization/bin/guest/serial.h"

class HostVsockAcceptor : public fuchsia::guest::HostVsockAcceptor {
 public:
  HostVsockAcceptor(uint32_t port, async::Loop* loop)
      : port_(port), console_(loop) {}

  // |fuchsia::guest::HostVsockAcceptor|
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

void handle_socat_listen(uint32_t env_id, uint32_t port, async::Loop* loop,
                         sys::ComponentContext* context) {
  fuchsia::guest::EnvironmentManagerSyncPtr environment_manager;
  context->svc()->Connect(environment_manager.NewRequest());
  fuchsia::guest::EnvironmentControllerSyncPtr environment_controller;
  environment_manager->Connect(env_id, environment_controller.NewRequest());
  fuchsia::guest::HostVsockEndpointSyncPtr vsock_endpoint;
  environment_controller->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  HostVsockAcceptor acceptor(port, loop);
  fidl::Binding<fuchsia::guest::HostVsockAcceptor> binding(&acceptor);
  zx_status_t status;
  vsock_endpoint->Listen(port, binding.NewBinding(), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to listen on port " << port << "\n";
    return;
  }

  loop->Run();
}

void handle_socat_connect(uint32_t env_id, uint32_t cid, uint32_t port,
                          async::Loop* loop, sys::ComponentContext* context) {
  fuchsia::guest::EnvironmentManagerSyncPtr environment_manager;
  context->svc()->Connect(environment_manager.NewRequest());
  fuchsia::guest::EnvironmentControllerSyncPtr environment_controller;
  environment_manager->Connect(env_id, environment_controller.NewRequest());
  fuchsia::guest::HostVsockEndpointSyncPtr vsock_endpoint;
  environment_controller->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

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

  SerialConsole console(loop);
  console.Start(std::move(socket));
  loop->Run();
}
