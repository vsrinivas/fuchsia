// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

extern mx_status_t ihda_init_hook(mx_driver_t*);
extern mx_status_t ihda_bind_hook(mx_driver_t*, mx_device_t*, void**);
extern void        ihda_unbind_hook(mx_driver_t*, mx_device_t*, void*);
extern mx_status_t ihda_release_hook(mx_driver_t*);

mx_driver_t _driver_intel_hda = {
    .ops = {
        .init = ihda_init_hook,
        .bind = ihda_bind_hook,
        .unbind = ihda_unbind_hook,
        .release = ihda_release_hook,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_intel_hda, "intel-hda", "magenta", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x2668), // Standard (Spec Rev 1.0a; 6/17/2010)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x9CA0), // Intel Broadwell PCH
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0xA170), // Intel 100/C230 PCH Spec
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x9D70), // Intel NUC
MAGENTA_DRIVER_END(_driver_intel_hda)
