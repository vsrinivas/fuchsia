// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VIRTUALIZATION_TESTING_FAKE_GUEST_VSOCK_H_
#define LIB_VIRTUALIZATION_TESTING_FAKE_GUEST_VSOCK_H_

#include <fuchsia/virtualization/cpp/fidl.h>

#include <unordered_map>

namespace guest {
namespace testing {
class FakeHostVsock;

// FakeGuestVsock provides a way to simulate a guest interation over the virtio-
// vsock bus in tests.
class FakeGuestVsock {
 public:
  FakeGuestVsock(FakeHostVsock* host_vsock) : host_vsock_(host_vsock) {}

  // Attempts to establish a vsock connection to the host endpoint on the given
  // port. For this to succeed, some other component must have already setup
  // a listener on that port using the
  // |fuchsia::virtualization::HostVsockEndpoint| returned from the enclosing
  // |fuchsia::virtualization::Realm|.
  using ConnectCallback = fit::function<void(zx::handle)>;
  zx_status_t ConnectToHost(uint32_t port, ConnectCallback callback);

  // Sets up a listener on the provided port to accept in-bound connections
  // from the host. The |acceptor| may be called multiple times if the host
  // attempts to establish multiple connections to the port.
  //
  // Returns |ZX_ERR_ALREADY_BOUND| if |Listen| has already been called with the
  // same port.
  using Acceptor = fit::function<zx_status_t(zx::handle)>;
  zx_status_t Listen(uint32_t port, Acceptor);

 protected:
  friend class FakeHostVsock;
  void AcceptConnectionFromHost(
      uint32_t port, zx::handle handle,
      fuchsia::virtualization::HostVsockEndpoint::ConnectCallback callback);

 private:
  FakeHostVsock* host_vsock_;
  std::unordered_map<uint32_t, Acceptor> listeners_;
};

}  // namespace testing
}  // namespace guest

#endif  // LIB_VIRTUALIZATION_TESTING_FAKE_GUEST_VSOCK_H_
