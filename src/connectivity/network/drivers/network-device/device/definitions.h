// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEFINITIONS_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEFINITIONS_H_

#include <fuchsia/hardware/network/llcpp/fidl.h>

#include <array>

#include <ddk/protocol/network/device.h>

#include "src/lib/vmo_store/vmo_store.h"

namespace network {
namespace netdev = llcpp::fuchsia::hardware::network;
constexpr uint16_t kMaxFifoDepth = ZX_PAGE_SIZE / sizeof(uint16_t);

namespace internal {
using BufferParts = std::array<buffer_region_t, MAX_BUFFER_PARTS>;
using DataVmoStore = vmo_store::VmoStore<vmo_store::SlabStorage<uint8_t>>;
}  // namespace internal

}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_DEFINITIONS_H_
