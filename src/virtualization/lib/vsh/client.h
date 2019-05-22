// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_VSH_CLIENT_H_
#define SRC_VIRTUALIZATION_LIB_VSH_CLIENT_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fit/result.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "src/virtualization/packages/biscotti_guest/third_party/protos/vsh.pb.h"

namespace vsh {

class BlockingClient {
 public:
  static fit::result<BlockingClient, zx_status_t> Connect(
      const fuchsia::virtualization::HostVsockEndpointSyncPtr& socket_endpoint,
      uint32_t cid, uint32_t port = 9001);

  vm_tools::vsh::ConnectionStatus status() const { return status_; }

  // Performs the initial connection setup flow.
  zx_status_t Setup(vm_tools::vsh::SetupConnectionRequest request);

  fit::result<vm_tools::vsh::HostMessage, zx_status_t> NextMessage();

 private:
  BlockingClient(zx::socket socket);

  zx::socket vsock_;
  vm_tools::vsh::ConnectionStatus status_ =
      vm_tools::vsh::ConnectionStatus::UNKNOWN;
};

}  // namespace vsh

#endif  // SRC_VIRTUALIZATION_LIB_VSH_CLIENT_H_
