// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/pci.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

#include <iostream>

#define QUALCOMM_VID 0x168c
#define QCA6174_DID  0x003e

static mx_status_t qca6174_bind(mx_driver_t* drv, mx_device_t* dev) {
    std::cout << __func__ << std::endl;
    mx_handle_t regs_handle = 0, config_handle = 0;
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci))
        return ERR_NOT_SUPPORTED;

    mx_status_t status = pci->claim_device(dev);
    if (status < 0)
        goto fail;

    const pci_config_t* pci_config;
    config_handle = pci->get_config(dev, &pci_config);
    if (config_handle < 0) {
        status = config_handle;
        goto fail;
    }

    void* regs;
    uint64_t regs_size;
    regs_handle = pci->map_mmio(
        dev, 0, MX_CACHE_POLICY_UNCACHED_DEVICE,
        (void**)&regs, &regs_size);
    if (regs_handle < 0) {
        status = regs_handle;
        goto fail;
    }

    std::cout << __func__ << ": SUCCESS" << std::endl;
    return NO_ERROR;

fail:
    if (regs_handle > 0)
        mx_handle_close(regs_handle);
    if (config_handle)
        mx_handle_close(config_handle);
    std::cout << __func__ << ": FAIL: " << status << std::endl;
    return status;
}

__BEGIN_CDECLS

mx_driver_t _driver_wifi_qca6174 = {
    .ops = {
        .bind = qca6174_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_wifi_qca6174, "wifi-qca6174", "fuchsia", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, QUALCOMM_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6174_DID),
MAGENTA_DRIVER_END(_driver_wifi_qca6174)

__END_CDECLS
