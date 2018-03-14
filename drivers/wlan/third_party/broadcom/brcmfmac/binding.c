// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brcm_hw_ids.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

uint64_t jiffies; // To make it link, jiffies has to be defined (not just declared)
struct current_with_pid* current; // likewise current

zx_status_t brcmfmac_bind(void* ctx, zx_device_t* device) {
    zxlogf(INFO, "brcmfmac: Bind was called!!\n");
    // TODO(cphoenix): Do the entry-point work starting at common.c:brcmfmac_module_init()
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_driver_ops_t brcmfmac_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = brcmfmac_bind,
};

ZIRCON_DRIVER_BEGIN(brcmfmac, brcmfmac_driver_ops, "zircon", "0.1", 21)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, BRCM_PCIE_VENDOR_ID_BROADCOM),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, PCI_CLASS_NETWORK),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, PCI_SUBCLASS_NETWORK_OTHER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4350_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4356_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43567_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43570_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4358_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4359_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_5G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_RAW_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_5G_DEVICE_ID),
 // BRCMF_PCIE_DEVICE_SUB(0x4365, BRCM_PCIE_VENDOR_ID_BROADCOM, 0x4365), // TODO(cphoenix): support
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_5G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4371_DEVICE_ID),
ZIRCON_DRIVER_END(brcmfmac)
