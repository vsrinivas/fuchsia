// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <zircon/device/ethernet.h>

#include <memory>

#include "fake_netstack_v1.h"

class FakeNetstack {
 public:
  FakeNetstack() = default;

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
  fake_netstack::v1::FakeState state_v1_;
  fake_netstack::v1::FakeNetstack netstack_v1_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
