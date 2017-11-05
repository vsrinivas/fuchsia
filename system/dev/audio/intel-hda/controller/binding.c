// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

extern zx_status_t ihda_init_hook(void**);
extern zx_status_t ihda_bind_hook(void*, zx_device_t*);
extern void        ihda_release_hook(void*);

static zx_driver_ops_t intel_hda_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = ihda_init_hook,
    .bind = ihda_bind_hook,
    .release = ihda_release_hook,
};

ZIRCON_DRIVER_BEGIN(intel_hda, intel_hda_driver_ops, "zircon", "0.1", 8)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x2668), // Standard (Spec Rev 1.0a; 6/17/2010)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x9CA0), // Intel Broadwell PCH
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0xA170), // Intel 100/C230 PCH Spec
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0xA1F0), // Intel 200/C400 PCH Spec
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x9D70), // Intel 6th Gen (Skylake) PCH-U/Y I/O Datasheet
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x9D71), // Intel 7th Gen (Skylake) PCH-U/Y I/O Datasheet
ZIRCON_DRIVER_END(intel_hda)
