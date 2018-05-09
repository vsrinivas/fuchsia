// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/socat.h"

#include <iostream>

#include <fuchsia/cpp/guest.h>

#include "garnet/bin/guest/cli/serial.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/tasks/message_loop.h"

class SocketAcceptor : public guest::SocketAcceptor {
 public:
  SocketAcceptor(uint32_t port) : port_(port) {}

  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) {
    FXL_CHECK(port == port_);
    zx::socket h1, h2;
    zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &h1, &h2);
    if (status != ZX_OK) {
      callback(ZX_ERR_CONNECTION_REFUSED, zx::socket());
      return;
    }
    callback(ZX_OK, std::move(h2));
    handle_serial(std::move(h1));
  }

 private:
  uint32_t port_;
};

void handle_socat_listen(uint32_t env_id, uint32_t port) {
  guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  guest::GuestEnvironmentSyncPtr guest_env;
  guestmgr->ConnectToEnvironment(env_id, guest_env.NewRequest());

  guest::ManagedSocketEndpointSyncPtr vsock_endpoint;
  guest_env->GetHostSocketEndpoint(vsock_endpoint.NewRequest());

  static SocketAcceptor acceptor(port);
  static fidl::Binding<guest::SocketAcceptor> binding(&acceptor);
  zx_status_t status;
  vsock_endpoint->Listen(port, binding.NewBinding(), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to listen on port " << port << "\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }
}

void handle_socat_connect(uint32_t env_id, uint32_t cid, uint32_t port) {
  guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  guest::GuestEnvironmentSyncPtr guest_env;
  guestmgr->ConnectToEnvironment(env_id, guest_env.NewRequest());

  zx_status_t status;
  zx::socket socket;
  guest::ManagedSocketEndpointSyncPtr vsock_endpoint;
  guest_env->GetHostSocketEndpoint(vsock_endpoint.NewRequest());

  vsock_endpoint->Connect(cid, port, &status, &socket);
  if (status != ZX_OK) {
    std::cerr << "Failed to connect " << status << "\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }
  handle_serial(std::move(socket));
}
