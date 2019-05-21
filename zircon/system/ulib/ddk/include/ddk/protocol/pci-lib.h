// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_PROTOCOL_PCI_LIB_H_
#define DDK_PROTOCOL_PCI_LIB_H_

#include <ddk/mmio-buffer.h>
#include <ddk/protocol/pci.h>
#include <stdio.h>
#include <zircon/syscalls.h>

__BEGIN_CDECLS

static inline zx_status_t pci_map_bar_buffer(const pci_protocol_t* pci, uint32_t bar_id,
                                             uint32_t cache_policy, mmio_buffer_t* buffer) {
    zx_pci_bar_t bar;
    zx_status_t st = pci->ops->get_bar(pci->ctx, bar_id, &bar);
    if (st != ZX_OK) {
        return st;
    }
    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_WRONG_TYPE;
    }

    size_t vmo_size;
    st = zx_vmo_get_size(bar.handle, &vmo_size);
    if (st != ZX_OK) {
        return st;
    }

    return  mmio_buffer_init(buffer, 0, vmo_size, bar.handle, cache_policy);
}

__END_CDECLS

#endif // DDK_PROTOCOL_PCI_LIB_H_
