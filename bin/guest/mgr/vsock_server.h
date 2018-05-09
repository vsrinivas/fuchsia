// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_VSOCK_SERVER_H_
#define GARNET_BIN_GUEST_MGR_VSOCK_SERVER_H_

#include <unordered_map>

#include "garnet/bin/guest/mgr/vsock_endpoint.h"

namespace guestmgr {

// Manages a set of |VsockEndpoint| objects addressed via associated context
// ID (CID) values.
class VsockServer {
 public:
  // Create a new |VsockEndpoint| with the given |cid|.
  //
  // Returns |ZX_ERR_ALREADY_BOUND| if |cid| is already in use and |ZX_OK|
  // otherwise.
  zx_status_t AddEndpoint(VsockEndpoint* endpoint);

  // Finds the |VsockEndpoint| addressed by |cid|. Returns |nullptr| if no
  // endpoint exists for |cid|.
  VsockEndpoint* FindEndpoint(uint32_t cid);

 private:
  // Friend |VsockEndpoint| to allow endpoints to remove themselves upon
  // deletion.
  friend class VsockEndpoint;
  void RemoveEndpoint(uint32_t cid);

  std::unordered_map<uint32_t, VsockEndpoint*> endpoints_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_VSOCK_SERVER_H_
