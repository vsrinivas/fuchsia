// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#include <mxalloc/new.h>
#include <mxtl/unique_ptr.h>

#include <magenta/compiler.h>
#include <magenta/types.h>

#include "block.h"
#include "device.h"
#include "gpu.h"
#include "trace.h"

#define LOCAL_TRACE 0

// implement driver object:

extern "C" mx_status_t virtio_bind(void* ctx, mx_device_t* device, void** cookie) {
    LTRACEF("device %p\n", device);
    mx_status_t status;
    pci_protocol_t* pci;

    /* grab the pci device and configuration */
    if (device_op_get_protocol(device, MX_PROTOCOL_PCI, (void**)&pci)) {
        TRACEF("no pci protocol\n");
        return -1;
    }

    const pci_config_t* config;
    mx_handle_t config_handle = MX_HANDLE_INVALID;
    status = pci->get_config(device, &config, &config_handle);
    if (status != NO_ERROR) {
        TRACEF("failed to grab config handle\n");
        return status;
    }

    LTRACEF("pci %p\n", pci);
    LTRACEF("0x%x:0x%x\n", config->vendor_id, config->device_id);

    mxtl::unique_ptr<virtio::Device> vd = nullptr;
    AllocChecker ac;
    switch (config->device_id) {
    case 0x1001:
        LTRACEF("found block device\n");
        vd.reset(new virtio::BlockDevice(device));
        break;
    case 0x1050:
        LTRACEF("found gpu device\n");
        vd.reset(new virtio::GpuDevice(device));
        break;
    default:
        printf("unhandled device id, how did this happen?\n");
        return -1;
    }

    LTRACEF("calling Bind on driver\n");
    status = vd->Bind(pci, config_handle, config);
    if (status != NO_ERROR)
        return status;

    status = vd->Init();
    if (status != NO_ERROR)
        return status;

    // if we're here, we're successful so drop the unique ptr ref to the object and let it live on
    vd.release();

    LTRACE_EXIT;

    return NO_ERROR;
}
