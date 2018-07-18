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

class VsockAcceptor : public fuchsia::guest::VsockAcceptor {
 public:
  VsockAcceptor(uint32_t port, async::Loop* loop)
      : port_(port), console_(loop) {}

  // |fuchsia::guest::VsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              zx::handle handle, AcceptCallback callback) override {
    if (port != port_) {
      callback(ZX_ERR_CONNECTION_REFUSED);
      return;
    }
    zx_obj_type_t type = fsl::GetType(handle.get());
    if (type != zx::socket::TYPE) {
      callback(ZX_ERR_CONNECTION_REFUSED);
      return;
    }
    callback(ZX_OK);
    console_.Start(zx::socket(std::move(handle)));
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

  fuchsia::guest::ManagedVsockEndpointSyncPtr vsock_endpoint;
  guest_env->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  VsockAcceptor acceptor(port, &loop);
  fidl::Binding<fuchsia::guest::VsockAcceptor> binding(&acceptor);
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

  zx_status_t status;
  zx::handle handle;
  fuchsia::guest::ManagedVsockEndpointSyncPtr vsock_endpoint;
  guest_env->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  vsock_endpoint->Connect(cid, port, &status, &handle);
  if (status != ZX_OK) {
    std::cerr << "Failed to connect " << status << "\n";
    return;
  }
  zx_obj_type_t type = fsl::GetType(handle.get());
  if (type != zx::socket::TYPE) {
    std::cerr << "Unexpected handle type " << type << "\n";
    return;
  }

  SerialConsole console(&loop);
  console.Start(zx::socket(std::move(handle)));
  loop.Run();
}
