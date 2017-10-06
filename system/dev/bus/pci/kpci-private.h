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
    PCI_OP_GET_AUXDATA,
    PCI_OP_MAX,
} pci_op_t;

extern _Atomic zx_txid_t pci_global_txid;

typedef struct {
    zx_txid_t txid;
    uint32_t len;
    uint32_t op;
} pci_req_hdr_t;

typedef struct {
    pci_req_hdr_t hdr;
    auxdata_type_t auxdata_type;
    auxdata_args_nth_device_t args;
} pci_req_auxdata_nth_device_t;

typedef struct {
    zx_txid_t txid;
    uint32_t len;
    uint32_t op;
    zx_status_t status;
} pci_resp_hdr_t;

typedef struct {
    pci_resp_hdr_t hdr;
    auxdata_i2c_device_t device;
} pci_resp_auxdata_i2c_nth_device_t;
