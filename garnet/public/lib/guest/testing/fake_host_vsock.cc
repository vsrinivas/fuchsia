// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/guest/testing/fake_guest_vsock.h>
#include <lib/guest/testing/fake_host_vsock.h>
#include <lib/guest/testing/guest_cid.h>

namespace guest {
namespace testing {

void FakeHostVsock::Listen(
    uint32_t port,
    fidl::InterfaceHandle<fuchsia::guest::HostVsockAcceptor> acceptor,
    ListenCallback callback) {
  if (listeners_.find(port) != listeners_.end()) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  listeners_.emplace(std::make_pair(port, acceptor.Bind()));
  callback(ZX_OK);
}

void FakeHostVsock::Connect(uint32_t cid, uint32_t port, zx::handle handle,
                            ConnectCallback callback) {
  if (cid != kGuestCid) {
    callback(ZX_ERR_INVALID_ARGS);
    return;
  }
  guest_vsock_->AcceptConnectionFromHost(port, std::move(handle),
                                         std::move(callback));
}

zx_status_t FakeHostVsock::AcceptConnectionFromGuest(
    uint32_t port, fit::function<void(zx::handle)> callback) {
  auto it = listeners_.find(port);
  if (it == listeners_.end()) {
    return ZX_ERR_CONNECTION_REFUSED;
  }
  it->second->Accept(
      kGuestCid, last_guest_port_--, port,
      [callback = std::move(callback)](auto status, auto handle) {
        if (status == ZX_OK) {
          callback(std::move(handle));
        } else {
          callback(zx::handle());
        }
      });
  return ZX_OK;
}

}  // namespace testing
}  // namespace guest
