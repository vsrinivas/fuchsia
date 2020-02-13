// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PCI_DRIVERS_AMLOGIC_PCIE_AML_PCIE_CLK_H_
#define SRC_DEVICES_PCI_DRIVERS_AMLOGIC_PCIE_AML_PCIE_CLK_H_

#include <lib/mmio/mmio.h>
#include <zircon/types.h>

zx_status_t PllSetRate(ddk::MmioBuffer* mmio);

#endif  // SRC_DEVICES_PCI_DRIVERS_AMLOGIC_PCIE_AML_PCIE_CLK_H_
