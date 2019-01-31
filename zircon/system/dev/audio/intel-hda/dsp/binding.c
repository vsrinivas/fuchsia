// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

#include "binding.h"

extern zx_status_t ihda_dsp_init_hook(void**);
extern zx_status_t ihda_dsp_bind_hook(void*, zx_device_t*);
extern void        ihda_dsp_release_hook(void*);

static zx_driver_ops_t intel_hda_dsp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = ihda_dsp_init_hook,
    .bind = ihda_dsp_bind_hook,
    .release = ihda_dsp_release_hook,
};

ZIRCON_DRIVER_BEGIN(intel_hda_dsp, intel_hda_dsp_driver_ops, "zircon", "0.1", 8)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_IHDA_DSP),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_HDA_PCI_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, INTEL_HDA_PCI_DID_KABYLAKE),
ZIRCON_DRIVER_END(intel_hda_dsp)
