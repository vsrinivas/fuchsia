// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_
#define GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_

#include "garnet/bin/guest/mgr/vsock_endpoint.h"

#include <unordered_map>

#include <bitmap/rle-bitmap.h>
#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

namespace guestmgr {

// Per:
// https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
static constexpr uint32_t kFirstEphemeralPort = 49152;
static constexpr uint32_t kLastEphemeralPort = 65535;

// Implements a |VsockEndpoint| to use for host connections. Specifically this
// endpoint will handle out-bound port allocations to avoid port collisions and
// exposes an interface for registering listeners on a per-port basis.
class HostVsockEndpoint : public VsockEndpoint,
                          public fuchsia::guest::ManagedVsockEndpoint {
 public:
  HostVsockEndpoint(uint32_t cid);
  ~HostVsockEndpoint() override;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::guest::ManagedVsockEndpoint> request);

  // |fuchsia::guest::VsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override;

  // |fuchsia::guest::ManagedVsockEndpoint|
  void Listen(uint32_t port, fidl::InterfaceHandle<VsockAcceptor> acceptor,
              ListenCallback callback) override;
  void Connect(
      uint32_t cid, uint32_t port,
      fuchsia::guest::ManagedVsockEndpoint::ConnectCallback callback) override;

  // This gets hidden by the ManagedVsockEndpoint::Connect overload so ensure
  // it's visible.
  using VsockEndpoint::Connect;

 private:
  struct Connection {
    uint32_t port;
    zx::handle handle;
    async::Wait wait;
  };

  void ConnectCallback(
      zx_status_t status, zx::handle handle, uint32_t src_port,
      fuchsia::guest::ManagedVsockEndpoint::ConnectCallback remote_callback);

  void OnPeerClosed(Connection* conn);

  zx_status_t FindEphemeralPort(uint32_t* port);
  zx_status_t FreePort(uint32_t port);

  bitmap::RleBitmap port_bitmap_;
  fidl::BindingSet<fuchsia::guest::ManagedVsockEndpoint> bindings_;
  std::unordered_map<uint32_t, fuchsia::guest::VsockAcceptorPtr> listeners_;
  std::unordered_map<uint32_t, std::unique_ptr<Connection>> connections_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_
