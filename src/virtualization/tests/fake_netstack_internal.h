// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_INTERNAL_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_INTERNAL_H_

#include <lib/fpromise/promise.h>
#include <zircon/status.h>

#include <vector>

namespace fake_netstack::internal {

class DeviceInterface {
 public:
  virtual ~DeviceInterface() = default;

  // Read the first available packet from the device.
  virtual fpromise::promise<std::vector<uint8_t>, zx_status_t> ReadPacket() = 0;

  // Send a packet to the device.
  virtual fpromise::promise<void, zx_status_t> WritePacket(std::vector<uint8_t> packet) = 0;
};

}  // namespace fake_netstack::internal

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_INTERNAL_H_
