// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_PCI_PCI_H_
#define SRC_DEVICES_LIB_PCI_PCI_H_

#include <fuchsia/hardware/syscalls/pci/c/banjo.h>
#include <zircon/syscalls/pci.h>

void zx_pci_device_info_to_banjo(zx_pcie_device_info_t src, pcie_device_info_t* dest);

void zx_pci_bar_to_banjo(zx_pci_bar_t src, pci_bar_t* dest);

#endif  // SRC_DEVICES_LIB_PCI_PCI_H_
