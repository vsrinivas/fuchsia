// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_BAR_INFO_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_BAR_INFO_H_

#include "allocation.h"

// Struct used to hold information about a configured base address register. This is shared
// between the Device class and MSI-X so it is held in its own header.
struct BarInfo {
  size_t size = 0;
  zx_paddr_t address = 0;  // Allocated address for the bar
  bool is_mmio;
  bool is_64bit;
  bool is_prefetchable;
  uint32_t bar_id;  // The bar index in the config space. If the bar is 64 bit
  // then this corresponds to the first half of the register pair
  std::unique_ptr<pci::PciAllocation> allocation;
};

#endif  // ZIRCON_SYSTEM_DEV_BUS_PCI_BAR_INFO_H_
