// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/driver.h>

extern struct zx_driver_ops msd_driver_ops;

// clang-format off
ZIRCON_DRIVER_BEGIN(magma_pci_gpu, msd_driver_ops, "zircon", "!0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, MAGMA_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
ZIRCON_DRIVER_END(magma_pci_gpu)
