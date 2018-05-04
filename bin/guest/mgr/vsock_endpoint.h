// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_VSOCK_ENDPOINT_H_
#define GARNET_BIN_GUEST_MGR_VSOCK_ENDPOINT_H_

#include <fuchsia/cpp/guest.h>
#include <unordered_map>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_handle.h"

namespace guestmgr {

class VsockServer;

// Maintains state associated with a single vsock endpoint within the system.
//
// A vsock endpoint either terminates inside a guest's virtio-vsock device
// implementation, or on the host. In the case of a guest, the virtio-vsock
// device will provide an impelemetation of |SocketAcceptor| that will be
// invoked whenever a request to connect to that CID is received.
//
// For the host, the |SocketAcceptor| implementation can be set via the
// |GuestEnvironment| interface.
//
// For both cases, we provide an implementation of |SocketConnector| that can
// be used for those components to establish out-bound socket connections. In
// both cases the provided |SocketConnector| is bound to the endpoints CID.
class VsockEndpoint : public guest::SocketConnector {
 public:
  VsockEndpoint(uint32_t cid, VsockServer* server);
  ~VsockEndpoint() override;

  uint32_t cid() const { return cid_; }

  // Called to bind both the |SocketConnector| and |SocketAcceptor| to a single
  // |SocketEndpoint|.
  void BindSocketEndpoint(guest::SocketEndpointPtr endpoint);

  // Binds |request| to a |SocketConnector| for this endpoint.
  void GetSocketConnector(
      fidl::InterfaceRequest<guest::SocketConnector> request);

  // Sets the |SocketAcceptor| to use for requests to this endpoint's |CID|.
  void SetSocketAcceptor(fidl::InterfaceHandle<guest::SocketAcceptor> handle);

  // |guest::SocketConnector|
  void Connect(uint32_t port,
               uint32_t dest_cid,
               uint32_t dest_port,
               ConnectCallback callback) override;

 private:
  uint32_t cid_;
  VsockServer* vsock_server_;
  fidl::BindingSet<guest::SocketConnector> connector_bindings_;
  guest::SocketAcceptorPtr remote_acceptor_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_VSOCK_ENDPOINT_H_
