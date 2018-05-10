// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/vsock_server.h"

#include "lib/fxl/logging.h"

namespace guestmgr {

zx_status_t VsockServer::CreateEndpoint(uint32_t cid,
                                        std::unique_ptr<VsockEndpoint>* out) {
  if (FindEndpoint(cid) != nullptr) {
    FXL_LOG(ERROR) << "CID " << cid << " is already bound";
    return ZX_ERR_ALREADY_BOUND;
  }

  auto endpoint = std::make_unique<VsockEndpoint>(cid, this);
  bool inserted;
  std::tie(std::ignore, inserted) = endpoints_.insert({cid, endpoint.get()});
  FXL_DCHECK(inserted);
  *out = std::move(endpoint);
  return ZX_OK;
}

void VsockServer::RemoveEndpoint(uint32_t cid) { endpoints_.erase(cid); }

VsockEndpoint* VsockServer::FindEndpoint(uint32_t cid) {
  auto it = endpoints_.find(cid);
  if (it == endpoints_.end()) {
    return nullptr;
  }
  return it->second;
}

}  //  namespace guestmgr
