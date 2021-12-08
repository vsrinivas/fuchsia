// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_

#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <zircon/device/ethernet.h>

#include <memory>

namespace fake_netstack::internal {
class FakeNetwork;
}

// Implements a fake netstack, providing the APIs:
//
//   * fuchsia.net.virtualization.Control
//   * fuchsia.netstack.NetStack
//
// and allowing packets to be sent and received from devices that attach to
// the fake netstack.
//
// Thread-safe.
class FakeNetstack {
 public:
  FakeNetstack();
  ~FakeNetstack();

  // Install FIDL services required for the fake netstack into the given environment.
  void Install(sys::testing::EnvironmentServices& services);

  // Send a packet with UDP headers, including the ethernet and IPv6 headers, to the interface with
  // the specified MAC address.
  fpromise::promise<void, zx_status_t> SendUdpPacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet);

  // Send a raw packet to the interface with the specified MAC address.
  fpromise::promise<void, zx_status_t> SendPacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet);

  // Receive a raw packet from the interface with the specified MAC address.
  fpromise::promise<std::vector<uint8_t>, zx_status_t> ReceivePacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr);

 private:
  async::Loop loop_;
  async::Executor executor_;

  // Fakes for fuchsia.net.virtualization.Control.
  std::unique_ptr<fake_netstack::internal::FakeNetwork> network_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
