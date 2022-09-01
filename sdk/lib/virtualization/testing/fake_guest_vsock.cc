// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtualization/testing/fake_guest_vsock.h>
#include <lib/virtualization/testing/fake_host_vsock.h>

#include "lib/fpromise/result.h"

namespace guest {
namespace testing {

zx_status_t FakeGuestVsock::ConnectToHost(uint32_t port, ConnectCallback callback) {
  return host_vsock_->AcceptConnectionFromGuest(port, std::move(callback));
}

zx_status_t FakeGuestVsock::Listen(uint32_t port, Acceptor acceptor) {
  if (listeners_.find(port) != listeners_.end()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  listeners_.emplace(std::make_pair(port, std::move(acceptor)));
  return ZX_OK;
}

void FakeGuestVsock::AcceptConnection2FromHost(
    uint32_t port, zx::socket client, zx::socket guest,
    fuchsia::virtualization::HostVsockEndpoint::ConnectCallback callback) {
  auto it = listeners_.find(port);
  if (it == listeners_.end()) {
    callback(fpromise::error(ZX_ERR_CONNECTION_REFUSED));
  } else {
    zx_status_t status = it->second(std::move(guest));
    if (status == ZX_OK) {
      callback(fpromise::ok(std::move(client)));
    } else {
      callback(fpromise::error(status));
    }
  }
}

}  // namespace testing
}  // namespace guest
