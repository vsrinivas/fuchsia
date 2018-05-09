// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/vsock_endpoint.h"

#include "garnet/bin/guest/mgr/vsock_server.h"
#include "lib/fxl/logging.h"

namespace guestmgr {

VsockEndpoint::VsockEndpoint(uint32_t cid) : cid_(cid) {}

VsockEndpoint::~VsockEndpoint() {
  if (vsock_server_ != nullptr) {
    vsock_server_->RemoveEndpoint(cid_);
    vsock_server_ = nullptr;
  }
}

void VsockEndpoint::Connect(uint32_t src_port, uint32_t dest_cid,
                            uint32_t dest_port, ConnectCallback callback) {
  FXL_DCHECK(vsock_server_);
  VsockEndpoint* dest = vsock_server_->FindEndpoint(dest_cid);
  if (dest == nullptr) {
    callback(ZX_ERR_CONNECTION_REFUSED, zx::socket());
    return;
  }
  dest->Accept(cid_, src_port, dest_port, std::move(callback));
}

}  //  namespace guestmgr
