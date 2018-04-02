// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/pciroot.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/types.h>

typedef struct kpci_device {
    zx_device_t* zxdev;

    // only set for non-shadow devices
    pciroot_protocol_t pciroot;
    platform_device_protocol_t pdev;

    // only set for shadow devices
    zx_handle_t pciroot_rpcch;

    // kernel pci handle, only set for shadow devices
    zx_handle_t handle;

    // nth device index
    uint32_t index;

    zx_pcie_device_info_t info;
} kpci_device_t;

typedef enum {
    PCI_OP_INVALID = 0,
    PCI_OP_RESET_DEVICE,
    PCI_OP_ENABLE_BUS_MASTER,
    PCI_OP_CONFIG_READ,
    PCI_OP_CONFIG_WRITE,
    PCI_OP_GET_BAR,
    PCI_OP_QUERY_IRQ_MODE,
    PCI_OP_SET_IRQ_MODE,
    PCI_OP_MAP_INTERRUPT,
    PCI_OP_GET_DEVICE_INFO,
    PCI_OP_GET_AUXDATA,
    PCI_OP_GET_BTI,
    PCI_OP_MAX,
} pci_op_t;

extern _Atomic zx_txid_t pci_global_txid;
static inline uint32_t pci_next_txid(void) {
    return atomic_fetch_add(&pci_global_txid, 1);
}

typedef struct {
    uint16_t offset;
    uint16_t width;
    uint32_t value;
} pci_msg_cfg_t;

// For use with QUERY_IRQ_MODE, SET_IRQ_MODE, and MAP_INTERRUPT
typedef struct {
    zx_pci_irq_mode_t mode;
    union {
        int32_t which_irq;
        uint32_t max_irqs;
        uint32_t requested_irqs;
    };
} pci_msg_irq_t;

#define PCI_MAX_DATA 4096
typedef struct {
    zx_txid_t txid; // FIDL message header
    uint32_t reserved0;
    uint32_t flags;
    uint32_t ordinal;

    uint32_t outlen;
    uint32_t datalen;
    // Subtract the size of the preceeding 6 uint32_ts to keep
    // the structure inside a page.
    union {
        bool enable;
        pci_msg_cfg_t cfg;
        pci_msg_irq_t irq;
        zx_pci_bar_t bar;
        zx_pcie_device_info_t info;
        uint8_t data[PCI_MAX_DATA - 24u];
        uint32_t bti_index;
    };
} pci_msg_t;
