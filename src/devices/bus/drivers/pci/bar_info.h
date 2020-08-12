// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_BAR_INFO_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_BAR_INFO_H_

#include "allocation.h"

// The public facing type for passing out bar information.
struct BarInfo {
  size_t size = 0;
  zx_paddr_t address = 0;
  uint8_t bar_id;
  bool is_mmio;
  bool is_64bit;
  bool is_prefetchable;
};

struct Bar {
  size_t size = 0;
  zx_paddr_t address = 0;  // Allocated address for the bar
  uint8_t bar_id;          // The bar index in the config space. If the bar is 64 bit
  bool is_mmio;
  bool is_64bit;
  bool is_prefetchable;
  // then this corresponds to the first half of the register pair
  std::unique_ptr<pci::PciAllocation> allocation;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_BAR_INFO_H_
