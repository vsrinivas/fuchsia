// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pciroot.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/hw/pci.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct pci_platform_info pci_platform_info_t;
typedef uint8_t pci_address_space_t;
#define PCI_ADDRESS_SPACE_MMIO UINT8_C(0)
#define PCI_ADDRESS_SPACE_IO UINT8_C(1)

typedef struct pci_irq_info pci_irq_info_t;
typedef struct msi_block msi_block_t;
typedef struct pciroot_protocol pciroot_protocol_t;

// Declarations

struct pci_platform_info {
    uint64_t reserved;
};

struct pci_irq_info {
    uint64_t reserved;
};

struct msi_block {
    uint64_t reserved;
};

typedef struct pciroot_protocol_ops {
    zx_status_t (*get_auxdata)(void* ctx, const char* args, void* out_data_buffer, size_t data_size,
                               size_t* out_data_actual);
    zx_status_t (*get_bti)(void* ctx, uint32_t bdf, uint32_t index, zx_handle_t* out_bti);
    zx_status_t (*get_pci_platform_info)(void* ctx, pci_platform_info_t* out_info);
    zx_status_t (*get_pci_irq_info)(void* ctx, pci_irq_info_t* out_info);
    zx_status_t (*driver_should_proxy_config)(void* ctx, bool* out_use_proxy);
    zx_status_t (*config_read8)(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                uint8_t* out_value);
    zx_status_t (*config_read16)(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                 uint16_t* out_value);
    zx_status_t (*config_read32)(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                 uint32_t* out_value);
    zx_status_t (*config_write8)(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                 uint8_t value);
    zx_status_t (*config_write16)(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                  uint16_t value);
    zx_status_t (*config_write32)(void* ctx, const pci_bdf_t* address, uint16_t offset,
                                  uint32_t value);
    zx_status_t (*msi_alloc_block)(void* ctx, uint64_t requested_irqs, bool can_target_64bit,
                                   msi_block_t* out_block);
    zx_status_t (*msi_free_block)(void* ctx, const msi_block_t* block);
    zx_status_t (*msi_mask_unmask)(void* ctx, uint64_t msi_id, bool mask);
    zx_status_t (*get_address_space)(void* ctx, size_t len, pci_address_space_t type, bool low,
                                     uint64_t* out_base);
    zx_status_t (*free_address_space)(void* ctx, uint64_t base, size_t len,
                                      pci_address_space_t type);
} pciroot_protocol_ops_t;

struct pciroot_protocol {
    pciroot_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t pciroot_get_auxdata(const pciroot_protocol_t* proto, const char* args,
                                              void* out_data_buffer, size_t data_size,
                                              size_t* out_data_actual) {
    return proto->ops->get_auxdata(proto->ctx, args, out_data_buffer, data_size, out_data_actual);
}
static inline zx_status_t pciroot_get_bti(const pciroot_protocol_t* proto, uint32_t bdf,
                                          uint32_t index, zx_handle_t* out_bti) {
    return proto->ops->get_bti(proto->ctx, bdf, index, out_bti);
}
static inline zx_status_t pciroot_get_pci_platform_info(const pciroot_protocol_t* proto,
                                                        pci_platform_info_t* out_info) {
    return proto->ops->get_pci_platform_info(proto->ctx, out_info);
}
static inline zx_status_t pciroot_get_pci_irq_info(const pciroot_protocol_t* proto,
                                                   pci_irq_info_t* out_info) {
    return proto->ops->get_pci_irq_info(proto->ctx, out_info);
}
static inline zx_status_t pciroot_driver_should_proxy_config(const pciroot_protocol_t* proto,
                                                             bool* out_use_proxy) {
    return proto->ops->driver_should_proxy_config(proto->ctx, out_use_proxy);
}
static inline zx_status_t pciroot_config_read8(const pciroot_protocol_t* proto,
                                               const pci_bdf_t* address, uint16_t offset,
                                               uint8_t* out_value) {
    return proto->ops->config_read8(proto->ctx, address, offset, out_value);
}
static inline zx_status_t pciroot_config_read16(const pciroot_protocol_t* proto,
                                                const pci_bdf_t* address, uint16_t offset,
                                                uint16_t* out_value) {
    return proto->ops->config_read16(proto->ctx, address, offset, out_value);
}
static inline zx_status_t pciroot_config_read32(const pciroot_protocol_t* proto,
                                                const pci_bdf_t* address, uint16_t offset,
                                                uint32_t* out_value) {
    return proto->ops->config_read32(proto->ctx, address, offset, out_value);
}
static inline zx_status_t pciroot_config_write8(const pciroot_protocol_t* proto,
                                                const pci_bdf_t* address, uint16_t offset,
                                                uint8_t value) {
    return proto->ops->config_write8(proto->ctx, address, offset, value);
}
static inline zx_status_t pciroot_config_write16(const pciroot_protocol_t* proto,
                                                 const pci_bdf_t* address, uint16_t offset,
                                                 uint16_t value) {
    return proto->ops->config_write16(proto->ctx, address, offset, value);
}
static inline zx_status_t pciroot_config_write32(const pciroot_protocol_t* proto,
                                                 const pci_bdf_t* address, uint16_t offset,
                                                 uint32_t value) {
    return proto->ops->config_write32(proto->ctx, address, offset, value);
}
static inline zx_status_t pciroot_msi_alloc_block(const pciroot_protocol_t* proto,
                                                  uint64_t requested_irqs, bool can_target_64bit,
                                                  msi_block_t* out_block) {
    return proto->ops->msi_alloc_block(proto->ctx, requested_irqs, can_target_64bit, out_block);
}
static inline zx_status_t pciroot_msi_free_block(const pciroot_protocol_t* proto,
                                                 const msi_block_t* block) {
    return proto->ops->msi_free_block(proto->ctx, block);
}
static inline zx_status_t pciroot_msi_mask_unmask(const pciroot_protocol_t* proto, uint64_t msi_id,
                                                  bool mask) {
    return proto->ops->msi_mask_unmask(proto->ctx, msi_id, mask);
}
static inline zx_status_t pciroot_get_address_space(const pciroot_protocol_t* proto, size_t len,
                                                    pci_address_space_t type, bool low,
                                                    uint64_t* out_base) {
    return proto->ops->get_address_space(proto->ctx, len, type, low, out_base);
}
static inline zx_status_t pciroot_free_address_space(const pciroot_protocol_t* proto, uint64_t base,
                                                     size_t len, pci_address_space_t type) {
    return proto->ops->free_address_space(proto->ctx, base, len, type);
}

__END_CDECLS;
