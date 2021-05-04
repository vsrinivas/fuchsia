// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/pci/pci.h"

void zx_pci_device_info_to_banjo(const zx_pcie_device_info_t src, pcie_device_info_t* dest) {
  dest->vendor_id = src.vendor_id;
  dest->device_id = src.device_id;
  dest->base_class = src.base_class;
  dest->sub_class = src.sub_class;
  dest->program_interface = src.program_interface;
  dest->revision_id = src.revision_id;
  dest->bus_id = src.bus_id;
  dest->dev_id = src.dev_id;
  dest->func_id = src.func_id;
  dest->padding1 = src.padding1;
}

void zx_pci_bar_to_banjo(const zx_pci_bar_t src, pci_bar_t* dest) {
  dest->id = src.id;
  dest->type = src.type;
  dest->size = src.size;
  dest->handle = src.handle;
}
