// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define A113_TDM_PHYS_BASE 0xff642000

// USB MMIO and IRQ
#define DWC3_MMIO_BASE      0xff500000
#define DWC3_MMIO_LENGTH    0x100000
#define DWC3_IRQ            62
#define USB_PHY_IRQ         48

// PCIe Resources
#define DW_PCIE_IRQ0        177
#define DW_PCIE_IRQ1        179

// Clock Control
#define AXG_HIU_BASE_PHYS 0xff63c000

