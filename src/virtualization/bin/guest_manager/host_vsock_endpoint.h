// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_HOST_VSOCK_ENDPOINT_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_HOST_VSOCK_ENDPOINT_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>

#include <deque>
#include <unordered_map>

#include <bitmap/rle-bitmap.h>

// Per:
// https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
static constexpr uint32_t kFirstEphemeralPort = 49152;
static constexpr uint32_t kLastEphemeralPort = 65535;

// How long to avoid reuse of ephemeral ports to avoid confusion between
// old and new connections.
//
// TODO(fxbug.dev/84286): Handle this in virtio-vsock.
constexpr zx::duration kPortQuarantineTime = zx::sec(10);

// An callback for querying a |Realm| for |GuestVsockAcceptor|s.
using AcceptorProvider = fit::function<fuchsia::virtualization::GuestVsockAcceptor*(uint32_t)>;

// An endpoint that represents the host. Specifically this endpoint will handle
// out-bound port allocations to avoid port collisions and exposes an interface
// for registering listeners on a per-port basis.
class HostVsockEndpoint : public fuchsia::virtualization::HostVsockConnector,
                          public fuchsia::virtualization::HostVsockEndpoint {
 public:
  HostVsockEndpoint(async_dispatcher_t* dispatcher, AcceptorProvider acceptor_provider);

  void AddBinding(fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> request);

  // |fuchsia::virtualization::HostVsockConnector|
  void Connect(uint32_t src_cid, uint32_t src_port, uint32_t cid, uint32_t port,
               fuchsia::virtualization::HostVsockConnector::ConnectCallback callback) override;

  // |fuchsia::virtualization::HostVsockEndpoint|
  void Listen(uint32_t port,
              fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor> acceptor,
              ListenCallback callback) override;
  void Connect(uint32_t cid, uint32_t port, zx::handle handle,
               fuchsia::virtualization::HostVsockEndpoint::ConnectCallback callback) override;

  void OnShutdown(uint32_t port);

 private:
  void ConnectCallback(zx_status_t status, uint32_t src_port,
                       fuchsia::virtualization::HostVsockEndpoint::ConnectCallback remote_callback);

  zx_status_t AllocEphemeralPort(uint32_t* port);
  void FreeEphemeralPort(uint32_t port);

  async_dispatcher_t* dispatcher_;  // Owned elsewhere.
  AcceptorProvider acceptor_provider_;
  bitmap::RleBitmapBase<uint32_t> port_bitmap_;

  // Recently freed ports, and the time they were freed. Used to reduce
  // reuse of ports in short time intervals to avoid packets from old
  // and new connections being confused.
  //
  // Ports are stored in non-decreasing release_time order.
  //
  // TODO(fxbug.dev/84286): Handle this in virtio-vsock.
  struct QuarantinedPort {
    uint32_t port;
    zx::time available_time;  // Time the port will become available again.
  };
  std::deque<QuarantinedPort> quarantined_ports_;

  fidl::BindingSet<fuchsia::virtualization::HostVsockEndpoint> bindings_;
  std::unordered_map<uint32_t, fuchsia::virtualization::HostVsockAcceptorPtr> listeners_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_HOST_VSOCK_ENDPOINT_H_
