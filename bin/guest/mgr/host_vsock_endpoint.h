// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_
#define GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_

#include <unordered_map>

#include <bitmap/rle-bitmap.h>
#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>

namespace guestmgr {

// Per:
// https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
static constexpr uint32_t kFirstEphemeralPort = 49152;
static constexpr uint32_t kLastEphemeralPort = 65535;

// An callback for querying a |GuestEnvironment| for |GuestVsockAcceptor|s.
using AcceptorProvider =
    fit::function<fuchsia::guest::GuestVsockAcceptor*(uint32_t)>;

// An endpoint that represents the host. Specifically this endpoint will handle
// out-bound port allocations to avoid port collisions and exposes an interface
// for registering listeners on a per-port basis.
class HostVsockEndpoint : public fuchsia::guest::HostVsockConnector,
                          public fuchsia::guest::HostVsockEndpoint {
 public:
  HostVsockEndpoint(AcceptorProvider acceptor_provider);

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> request);

  // |fuchsia::guest::HostVsockConnector|
  void Connect(
      uint32_t src_cid, uint32_t src_port, uint32_t cid, uint32_t port,
      fuchsia::guest::HostVsockConnector::ConnectCallback callback) override;

  // |fuchsia::guest::HostVsockEndpoint|
  void Listen(uint32_t port,
              fidl::InterfaceHandle<fuchsia::guest::HostVsockAcceptor> acceptor,
              ListenCallback callback) override;
  void Connect(
      uint32_t cid, uint32_t port, zx::handle handle,
      fuchsia::guest::HostVsockEndpoint::ConnectCallback callback) override;

 private:
  struct Connection {
    uint32_t port;
    zx::handle handle;
    async::Wait wait;
  };

  void ConnectCallback(
      zx_status_t status, zx::handle dup, uint32_t src_port,
      fuchsia::guest::HostVsockEndpoint::ConnectCallback remote_callback);

  void OnPeerClosed(Connection* conn);

  zx_status_t AllocEphemeralPort(uint32_t* port);
  void FreeEphemeralPort(uint32_t port);

  AcceptorProvider acceptor_provider_;
  bitmap::RleBitmap port_bitmap_;
  fidl::BindingSet<fuchsia::guest::HostVsockEndpoint> bindings_;
  std::unordered_map<uint32_t, fuchsia::guest::HostVsockAcceptorPtr> listeners_;
  std::unordered_map<uint32_t, std::unique_ptr<Connection>> connections_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_HOST_VSOCK_ENDPOINT_H_
