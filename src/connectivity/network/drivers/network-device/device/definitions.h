// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEFINITIONS_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEFINITIONS_H_

#include <fuchsia/hardware/network/llcpp/fidl.h>

#include <array>

namespace network {
namespace netdev = llcpp::fuchsia::hardware::network;
constexpr uint32_t kMaxFifoDepth = ZX_PAGE_SIZE / sizeof(uint16_t);

namespace internal {
using VirtualMemParts = std::array<buffer_region_t, MAX_VIRTUAL_PARTS>;
using PhysicalMemParts = std::array<phys_entry_t, MAX_PHYSICAL_PARTS>;
}  // namespace internal

}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEFINITIONS_H_
