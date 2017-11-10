// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/pciroot.h>
#include <zircon/types.h>

typedef struct kpci_device {
    zx_device_t* zxdev;

    // only set for non-shadow devices
    pciroot_protocol_t pciroot;

    // only set for shadow devices
    zx_handle_t pciroot_rpcch;

    // kernel pci handle, only set for shadow devices
    zx_handle_t handle;

    // nth device index
    uint32_t index;

    zx_pcie_device_info_t info;
} kpci_device_t;

typedef enum {
    PCI_OP_RESET_DEVICE = 1,
    PCI_OP_ENABLE_BUS_MASTER,
    PCI_OP_ENABLE_PIO,
    PCI_OP_CONFIG_READ,
    PCI_OP_GET_NEXT_CAPABILITY,
    PCI_OP_GET_RESOURCE,
    PCI_OP_MAP_RESOURCE,
    PCI_OP_QUERY_IRQ_MODE_CAPS,
    PCI_OP_SET_IRQ_MODE,
    PCI_OP_MAP_INTERRUPT,
    PCI_OP_GET_DEVICE_INFO,
    PCI_OP_GET_AUXDATA,
    PCI_OP_MAX,
} pci_op_t;

extern _Atomic zx_txid_t pci_global_txid;
static inline uint32_t pci_next_txid(void) {
    return atomic_fetch_add(&pci_global_txid, 1);
}

#define PCI_MAX_DATA 4096

typedef struct {
    zx_txid_t txid;     // FIDL2 message header
    uint32_t reserved0;
    uint32_t flags;
    uint32_t ordinal;

    uint32_t outlen;
    uint32_t datalen;
    uint8_t data[PCI_MAX_DATA];
} pci_msg_t;