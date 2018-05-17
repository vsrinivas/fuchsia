// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_REMOTE_VSOCK_ENDPOINT_H_
#define GARNET_BIN_GUEST_MGR_REMOTE_VSOCK_ENDPOINT_H_

#include "garnet/bin/guest/mgr/vsock_endpoint.h"

#include <fuchsia/guest/cpp/fidl.h>

namespace guestmgr {

// A |VsockEndpoint| that delegates most work to a remote endpoint using the
// |fuchsia::guest::SocketAcceptor| and |fuchsia::guest::SocketConnector|
// interfaces. Specifically the remote component is responsible for the
// allocation of out-bound ports and accepting all in-bound connections.
//
// For example, a guest vsock driver will maintain state around what ports are
// being listened on and track ephemeral port usage for out-bound connections.
class RemoteVsockEndpoint : public VsockEndpoint {
 public:
  RemoteVsockEndpoint(uint32_t cid);
  ~RemoteVsockEndpoint() override;

  // Called to bind both the |SocketConnector| and |SocketAcceptor| to a single
  // |SocketEndpoint|.
  void BindSocketEndpoint(fuchsia::guest::SocketEndpointPtr endpoint);

  // Binds |request| to a |SocketConnector| for this endpoint.
  void GetSocketConnector(
      fidl::InterfaceRequest<fuchsia::guest::SocketConnector> request);

  // Sets the |SocketAcceptor| to use for requests to this endpoint's |CID|.
  void SetSocketAcceptor(
      fidl::InterfaceHandle<fuchsia::guest::SocketAcceptor> handle);

  // |fuchsia::guest::SocketAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              AcceptCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::guest::SocketConnector> connector_bindings_;
  fuchsia::guest::SocketAcceptorPtr remote_acceptor_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_REMOTE_VSOCK_ENDPOINT_H_
