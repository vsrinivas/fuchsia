// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_VSOCK_ENDPOINT_H_
#define GARNET_BIN_GUEST_MGR_GUEST_VSOCK_ENDPOINT_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

namespace guestmgr {

// An endpoint that represents a guest. This endpoint delegates work to the
// guest using the |fuchsia::guest::GuestVsockAcceptor| interface. Specifically
// the guest is responsible for the allocation of outbound ports and accepting
// all inbound connections.
class GuestVsockEndpoint : public fuchsia::guest::GuestVsockAcceptor {
 public:
  GuestVsockEndpoint(uint32_t cid,
                     fuchsia::guest::GuestVsockEndpointPtr guest_endpoint,
                     fuchsia::guest::HostVsockConnector* connector);

 private:
  // |fuchsia::guest::GuestVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              zx::handle handle, AcceptCallback callback) override;

  fidl::Binding<fuchsia::guest::HostVsockConnector> connector_binding_;
  fuchsia::guest::GuestVsockAcceptorPtr acceptor_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_VSOCK_ENDPOINT_H_
