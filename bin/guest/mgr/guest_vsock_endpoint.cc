// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_vsock_endpoint.h"

namespace guestmgr {

GuestVsockEndpoint::GuestVsockEndpoint(
    uint32_t cid, fuchsia::guest::GuestVsockEndpointPtr guest_endpoint,
    fuchsia::guest::HostVsockConnector* connector)
    : connector_binding_(connector) {
  guest_endpoint->SetContextId(cid, connector_binding_.NewBinding(),
                               acceptor_.NewRequest());
}

void GuestVsockEndpoint::Accept(uint32_t src_cid, uint32_t src_port,
                                uint32_t port, zx::handle handle,
                                AcceptCallback callback) {
  acceptor_->Accept(src_cid, src_port, port, std::move(handle),
                    std::move(callback));
}

}  //  namespace guestmgr
