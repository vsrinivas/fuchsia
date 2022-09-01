// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtualization/testing/fake_guest_vsock.h>
#include <lib/virtualization/testing/fake_host_vsock.h>
#include <lib/virtualization/testing/guest_cid.h>

namespace guest {
namespace testing {

using ::fuchsia::virtualization::HostVsockAcceptor_Accept_Result;
using ::fuchsia::virtualization::HostVsockEndpoint_Listen_Result;

void FakeHostVsock::Listen(
    uint32_t port, fidl::InterfaceHandle<fuchsia::virtualization::HostVsockAcceptor> acceptor,
    ListenCallback callback) {
  if (listeners_.find(port) != listeners_.end()) {
    callback(HostVsockEndpoint_Listen_Result::WithErr(ZX_ERR_ALREADY_BOUND));
    return;
  }
  listeners_.emplace(std::make_pair(port, acceptor.Bind()));
  callback(HostVsockEndpoint_Listen_Result::WithResponse({}));
}

void FakeHostVsock::Connect(uint32_t port, ConnectCallback callback) {
  zx::socket client, guest;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &client, &guest);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
    return;
  }
  guest_vsock_->AcceptConnection2FromHost(port, std::move(client), std::move(guest),
                                          std::move(callback));
}

zx_status_t FakeHostVsock::AcceptConnectionFromGuest(uint32_t port,
                                                     fit::function<void(zx::handle)> callback) {
  auto it = listeners_.find(port);
  if (it == listeners_.end()) {
    return ZX_ERR_CONNECTION_REFUSED;
  }
  it->second->Accept(kGuestCid, last_guest_port_--, port,
                     [callback = std::move(callback)](HostVsockAcceptor_Accept_Result result) {
                       if (result.is_response()) {
                         callback(std::move(result.response().socket));
                       } else {
                         callback(zx::socket());
                       }
                     });
  return ZX_OK;
}

}  // namespace testing
}  // namespace guest
