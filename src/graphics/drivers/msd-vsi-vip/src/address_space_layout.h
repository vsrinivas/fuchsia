// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_ADDRESS_SPACE_LAYOUT_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_ADDRESS_SPACE_LAYOUT_H_

#include <stdint.h>

#include "macros.h"

class AddressSpaceLayout {
 public:
  static constexpr uint32_t kRingbufferSizeInPages = 1;

  static constexpr uint32_t kRingbufferBlankPages = 3;

  // Returns whether the range [start_gpu_addr, end_gpu_addr) lies within the
  // client reserved region.
  static bool IsValidClientGpuRange(uint64_t start_gpu_addr, uint64_t end_gpu_addr) {
    return (start_gpu_addr >= client_gpu_addr_base()) &&
           (end_gpu_addr <= (client_gpu_addr_base() + client_gpu_addr_size()));
  }

  static uint32_t ringbuffer_size() { return kRingbufferSizeInPages * magma::page_size(); }

  static uint32_t system_gpu_addr_size() {
    return (kRingbufferSizeInPages + kRingbufferBlankPages) * magma::page_size();
  }

  static uint32_t client_gpu_addr_size() { return (1ull << 31) - system_gpu_addr_size(); }

  static uint32_t client_gpu_addr_base() { return 0; }

  static uint32_t system_gpu_addr_base() { return client_gpu_addr_base() + client_gpu_addr_size(); }
};

#endif
