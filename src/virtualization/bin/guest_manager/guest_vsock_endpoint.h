// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_VSOCK_ENDPOINT_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_VSOCK_ENDPOINT_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

// An endpoint that represents a guest. This endpoint delegates work to the
// guest using the |fuchsia::virtualization::GuestVsockAcceptor| interface.
// Specifically the guest is responsible for the allocation of outbound ports
// and accepting all inbound connections.
class GuestVsockEndpoint : public fuchsia::virtualization::GuestVsockAcceptor {
 public:
  GuestVsockEndpoint(
      uint32_t cid,
      fuchsia::virtualization::GuestVsockEndpointPtr guest_endpoint,
      fuchsia::virtualization::HostVsockConnector* connector);

 private:
  // |fuchsia::virtualization::GuestVsockAcceptor|
  void Accept(uint32_t src_cid, uint32_t src_port, uint32_t port,
              zx::handle handle, AcceptCallback callback) override;

  fidl::Binding<fuchsia::virtualization::HostVsockConnector> connector_binding_;
  fuchsia::virtualization::GuestVsockAcceptorPtr acceptor_;
  fuchsia::virtualization::GuestVsockEndpointPtr guest_endpoint_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_VSOCK_ENDPOINT_H_
