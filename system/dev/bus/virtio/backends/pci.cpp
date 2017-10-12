// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of tag() source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/debug.h>
#include <ddk/protocol/pci.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <virtio/virtio.h>
#include <zx/handle.h>

#include "pci.h"

namespace virtio {

PciBackend::PciBackend(pci_protocol_t pci, zx_pcie_device_info_t info)
    : pci_(pci), info_(info) {
    snprintf(tag_, sizeof(tag_), "pci[%02x:%02x.%1x]", info_.bus_id, info_.dev_id, info_.func_id);
}

zx_status_t PciBackend::Bind() {
    zx_handle_t tmp_handle;

    // enable bus mastering
    zx_status_t r;
    if ((r = pci_enable_bus_master(&pci_, true)) != ZX_OK) {
        zxlogf(ERROR, "%s: cannot enable bus master %d\n", tag(), r);
        return r;
    }

    // try to set up our IRQ mode
    if (pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_MSI, 1)) {
        if (pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_LEGACY, 1)) {
            zxlogf(ERROR, "%s: failed to set irq mode\n", tag());
            return -1;
        } else {
            zxlogf(SPEW, "%s: using legacy irq mode\n", tag());
        }
    }

    r = pci_map_interrupt(&pci_, 0, &tmp_handle);
    if (r != ZX_OK) {
        zxlogf(ERROR, "%s: failed to map irq %d\n", tag(), r);
        return r;
    }
    irq_handle_.reset(tmp_handle);

    zxlogf(SPEW, "%s: irq handle %u\n", tag(), irq_handle_.get());

    return Init();
}

} // namespace virtio
