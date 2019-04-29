// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_PROTOCOL_PCI_LIB_H_
#define DDK_PROTOCOL_PCI_LIB_H_

#include <ddk/protocol/pci.h>
#include <ddk/mmio-buffer.h>

__BEGIN_CDECLS

static inline zx_status_t pci_map_bar_buffer(const pci_protocol_t* pci, uint32_t bar_id,
                                      uint32_t cache_policy, mmio_buffer_t* buffer) {

    zx_pci_bar_t bar;

    zx_status_t status = pci->ops->get_bar(pci->ctx, bar_id, &bar);
    if (status != ZX_OK) {
        return status;
    }
    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (bar.type == ZX_PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_WRONG_TYPE;
    }
    return mmio_buffer_init(buffer, 0, bar.size, bar.handle, cache_policy);
}

static inline zx_status_t pci_config_read8(const pci_protocol_t* pci,
                                           uint16_t offset, uint8_t* value) {
    uint32_t value_;
    zx_status_t st = pci->ops->config_read(pci->ctx, offset, sizeof(uint8_t), &value_);
    *value = value_ & UINT8_MAX;
    return st;
}

static inline zx_status_t pci_config_read16(const pci_protocol_t* pci,
                                            uint16_t offset, uint16_t* value) {
    uint32_t value_;
    zx_status_t st = pci->ops->config_read(pci->ctx, offset, sizeof(uint16_t), &value_);
    *value = value_ & UINT16_MAX;
    return st;
}

static inline zx_status_t pci_config_read32(const pci_protocol_t* pci,
                                            uint16_t offset, uint32_t* value) {
    return pci->ops->config_read(pci->ctx, offset, sizeof(uint32_t), value);
}

static inline zx_status_t pci_config_write8(const pci_protocol_t* pci,
                                            uint16_t offset, uint8_t value) {
    return pci->ops->config_write(pci->ctx, offset, sizeof(uint8_t), value);
}

static inline zx_status_t pci_config_write16(const pci_protocol_t* pci,
                                             uint16_t offset, uint16_t value) {
    return pci->ops->config_write(pci->ctx, offset, sizeof(uint16_t), value);
}

static inline zx_status_t pci_config_write32(const pci_protocol_t* pci,
                                             uint16_t offset, uint32_t value) {
    return pci->ops->config_write(pci->ctx, offset, sizeof(uint32_t), value);
}

__END_CDECLS

#endif  // DDK_PROTOCOL_PCI_LIB_H_
