// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pci.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct pci_protocol pci_protocol_t;
typedef uint8_t pci_cap_id_t;
#define PCI_CAP_ID_NULL UINT8_C(0)
#define PCI_CAP_ID_PCI_PWR_MGMT UINT8_C(1)
#define PCI_CAP_ID_AGP UINT8_C(2)
#define PCI_CAP_ID_VPD UINT8_C(3)
#define PCI_CAP_ID_MSI UINT8_C(5)
#define PCI_CAP_ID_PCIX UINT8_C(7)
#define PCI_CAP_ID_HYPERTRANSPORT UINT8_C(8)
#define PCI_CAP_ID_VENDOR UINT8_C(9)
#define PCI_CAP_ID_DEBUG_PORT UINT8_C(10)
#define PCI_CAP_ID_COMPACT_PCI_CRC UINT8_C(11)
#define PCI_CAP_ID_PCI_HOT_PLUG UINT8_C(12)
#define PCI_CAP_ID_PCI_BRIDGE_SUBSYSTEM_VID UINT8_C(13)
#define PCI_CAP_ID_AGP8X UINT8_C(14)
#define PCI_CAP_ID_SECURE_DEVICE UINT8_C(15)
#define PCI_CAP_ID_PCI_EXPRESS UINT8_C(16)
#define PCI_CAP_ID_MSIX UINT8_C(17)
#define PCI_CAP_ID_SATA_DATA_NDX_CFG UINT8_C(18)
#define PCI_CAP_ID_ADVANCED_FEATURES UINT8_C(19)
#define PCI_CAP_ID_ENHANCED_ALLOCATION UINT8_C(20)

typedef uint16_t pci_cfg_t;
#define PCI_CFG_VENDOR_ID UINT16_C(0)
#define PCI_CFG_DEVICE_ID UINT16_C(2)
#define PCI_CFG_REVISION_ID UINT16_C(8)
#define PCI_CFG_CLASS_CODE UINT16_C(9)
#define PCI_CFG_SUBSYSTEM_VENDOR_ID UINT16_C(44)
#define PCI_CFG_SUBSYSTEM_ID UINT16_C(46)
#define PCI_CFG_CAPABILITIES_PTR UINT16_C(52)

// Declarations

typedef struct pci_protocol_ops {
    zx_status_t (*get_bar)(void* ctx, uint32_t bar_id, zx_pci_bar_t* out_res);
    zx_status_t (*map_bar)(void* ctx, uint32_t bar_id, uint32_t cache_policy,
                           void** out_vaddr_buffer, size_t* vaddr_size, zx_handle_t* out_handle);
    zx_status_t (*enable_bus_master)(void* ctx, bool enable);
    zx_status_t (*reset_device)(void* ctx);
    zx_status_t (*map_interrupt)(void* ctx, zx_status_t which_irq, zx_handle_t* out_handle);
    zx_status_t (*query_irq_mode)(void* ctx, zx_pci_irq_mode_t mode, uint32_t* out_max_irqs);
    zx_status_t (*set_irq_mode)(void* ctx, zx_pci_irq_mode_t mode, uint32_t requested_irq_count);
    zx_status_t (*get_device_info)(void* ctx, zx_pcie_device_info_t* out_into);
    zx_status_t (*config_read)(void* ctx, uint16_t offset, size_t width, uint32_t* out_value);
    zx_status_t (*config_write)(void* ctx, uint16_t offset, size_t width, uint32_t value);
    uint8_t (*get_next_capability)(void* ctx, uint8_t type, uint8_t offset);
    zx_status_t (*get_auxdata)(void* ctx, const char* args, void* out_data_buffer, size_t data_size,
                               size_t* out_data_actual);
    zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_bti);
} pci_protocol_ops_t;

struct pci_protocol {
    pci_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t pci_get_bar(const pci_protocol_t* proto, uint32_t bar_id,
                                      zx_pci_bar_t* out_res) {
    return proto->ops->get_bar(proto->ctx, bar_id, out_res);
}
static inline zx_status_t pci_map_bar(const pci_protocol_t* proto, uint32_t bar_id,
                                      uint32_t cache_policy, void** out_vaddr_buffer,
                                      size_t* vaddr_size, zx_handle_t* out_handle) {
    return proto->ops->map_bar(proto->ctx, bar_id, cache_policy, out_vaddr_buffer, vaddr_size,
                               out_handle);
}
static inline zx_status_t pci_enable_bus_master(const pci_protocol_t* proto, bool enable) {
    return proto->ops->enable_bus_master(proto->ctx, enable);
}
static inline zx_status_t pci_reset_device(const pci_protocol_t* proto) {
    return proto->ops->reset_device(proto->ctx);
}
static inline zx_status_t pci_map_interrupt(const pci_protocol_t* proto, zx_status_t which_irq,
                                            zx_handle_t* out_handle) {
    return proto->ops->map_interrupt(proto->ctx, which_irq, out_handle);
}
static inline zx_status_t pci_query_irq_mode(const pci_protocol_t* proto, zx_pci_irq_mode_t mode,
                                             uint32_t* out_max_irqs) {
    return proto->ops->query_irq_mode(proto->ctx, mode, out_max_irqs);
}
static inline zx_status_t pci_set_irq_mode(const pci_protocol_t* proto, zx_pci_irq_mode_t mode,
                                           uint32_t requested_irq_count) {
    return proto->ops->set_irq_mode(proto->ctx, mode, requested_irq_count);
}
static inline zx_status_t pci_get_device_info(const pci_protocol_t* proto,
                                              zx_pcie_device_info_t* out_into) {
    return proto->ops->get_device_info(proto->ctx, out_into);
}
static inline zx_status_t pci_config_read(const pci_protocol_t* proto, uint16_t offset,
                                          size_t width, uint32_t* out_value) {
    return proto->ops->config_read(proto->ctx, offset, width, out_value);
}
static inline zx_status_t pci_config_write(const pci_protocol_t* proto, uint16_t offset,
                                           size_t width, uint32_t value) {
    return proto->ops->config_write(proto->ctx, offset, width, value);
}
static inline uint8_t pci_get_next_capability(const pci_protocol_t* proto, uint8_t type,
                                              uint8_t offset) {
    return proto->ops->get_next_capability(proto->ctx, type, offset);
}
static inline zx_status_t pci_get_auxdata(const pci_protocol_t* proto, const char* args,
                                          void* out_data_buffer, size_t data_size,
                                          size_t* out_data_actual) {
    return proto->ops->get_auxdata(proto->ctx, args, out_data_buffer, data_size, out_data_actual);
}
static inline zx_status_t pci_get_bti(const pci_protocol_t* proto, uint32_t index,
                                      zx_handle_t* out_bti) {
    return proto->ops->get_bti(proto->ctx, index, out_bti);
}

__END_CDECLS;
