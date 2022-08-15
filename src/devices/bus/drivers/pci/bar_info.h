// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_BAR_INFO_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_BAR_INFO_H_

#include <optional>

#include "src/devices/bus/drivers/pci/allocation.h"

struct Bar {
  zx_paddr_t address = 0;  // Allocated address for the bar
  size_t size = 0;
  uint8_t bar_id;  // The bar index in the config space. If the bar is 64 bit
  bool is_mmio;    // then the id represents the lowerindex of the two.
  bool is_64bit;   // This bar is the lower half of a 64 bit bar at |bar_id|.
  bool is_prefetchable;
  // then this corresponds to the first half of the register pair
  std::unique_ptr<pci::PciAllocation> allocation;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_BAR_INFO_H_
