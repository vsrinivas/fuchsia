// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GUEST_TESTING_FAKE_HOST_VSOCK_H_
#define LIB_GUEST_TESTING_FAKE_HOST_VSOCK_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

namespace guest {
namespace testing {

class FakeGuestVsock;

class FakeHostVsock : public fuchsia::guest::HostVsockEndpoint {
 public:
  FakeHostVsock(FakeGuestVsock* guest_vsock) : guest_vsock_(guest_vsock) {}

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint) {
    bindings_.AddBinding(this, std::move(endpoint));
  }

 protected:
  friend class FakeGuestVsock;
  zx_status_t AcceptConnectionFromGuest(
      uint32_t port, fit::function<void(zx::handle)> callback);

 private:
  // |fuchsia::guest::HostVsockEndpoint|
  void Listen(uint32_t port,
              fidl::InterfaceHandle<fuchsia::guest::HostVsockAcceptor> acceptor,
              ListenCallback callback) override;
  void Connect(uint32_t cid, uint32_t port, zx::handle handle,
               ConnectCallback callback) override;

  FakeGuestVsock* guest_vsock_;
  fidl::BindingSet<fuchsia::guest::HostVsockEndpoint> bindings_;
  // The set of vsock ports that are being listened on. The HostVsockAcceptorPtr
  // will handle any simulated in-bound requests from the guest.
  std::unordered_map<uint32_t, fuchsia::guest::HostVsockAcceptorPtr> listeners_;
  // The outbound port number from the guest for vsock connections. To be
  // decremented on each connection.
  uint16_t last_guest_port_ = UINT16_MAX;
};

}  // namespace testing
}  // namespace guest

#endif  // LIB_GUEST_TESTING_FAKE_HOST_VSOCK_H_
