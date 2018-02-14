// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

#include "binding.h"

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
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_HDA_PCI_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_STANDARD),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_BROADWELL),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_100_C230),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_200_C400),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_SKYLAKE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_KABYLAKE),
ZIRCON_DRIVER_END(intel_hda)
