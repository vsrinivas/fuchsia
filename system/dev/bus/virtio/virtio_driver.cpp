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

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

#include "block.h"
#include "console.h"
#include "device.h"
#include "rng.h"
#include "ethernet.h"
#include "gpu.h"
#include "input.h"
#include "trace.h"

#define LOCAL_TRACE 0

// implement driver object:

extern "C" zx_status_t virtio_bind(void* ctx, zx_device_t* device, void** cookie) {
    LTRACEF("device %p\n", device);
    zx_status_t status;
    pci_protocol_t pci;

    /* grab the pci device and configuration */
    if (device_get_protocol(device, ZX_PROTOCOL_PCI, &pci)) {
        TRACEF("no pci protocol\n");
        return -1;
    }

    const pci_config_t* config;
    size_t config_size;
    zx_handle_t config_handle = ZX_HANDLE_INVALID;
    status = pci_map_resource(&pci, PCI_RESOURCE_CONFIG, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   (void**)&config, &config_size, &config_handle);
    if (status != ZX_OK) {
        TRACEF("failed to grab config handle\n");
        return status;
    }

    LTRACEF("pci %p\n", &pci);
    LTRACEF("0x%x:0x%x\n", config->vendor_id, config->device_id);

    // TODO: Make symbols for these constants and reuse in the BIND protocol.
    fbl::unique_ptr<virtio::Device> vd = nullptr;
    switch (config->device_id) {
    case 0x1000:
        LTRACEF("found net device\n");
        vd.reset(new virtio::EthernetDevice(device));
        break;
    case 0x1001:
    case 0x1042:
        LTRACEF("found block device\n");
        vd.reset(new virtio::BlockDevice(device));
        break;
    case 0x1003:
    case 0x1043:
        LTRACEF("found console device\n");
        vd.reset(new virtio::ConsoleDevice(device));
        break;
    case 0x1050:
        LTRACEF("found gpu device\n");
        vd.reset(new virtio::GpuDevice(device));
        break;
    case 0x1005:
    case 0x1044:
        LTRACEF("found entropy device\n");
        vd.reset(new virtio::RngDevice(device));
        break;
    case 0x1052:
        LTRACEF("found input device\n");
        vd.reset(new virtio::InputDevice(device));
        break;
    default:
        printf("unhandled device id, how did this happen?\n");
        return -1;
    }

    LTRACEF("calling Bind on driver\n");
    status = vd->Bind(&pci, config_handle, config);
    if (status != ZX_OK)
        return status;

    status = vd->Init();
    if (status != ZX_OK)
        return status;

    // if we're here, we're successful so drop the unique ptr ref to the object and let it live on
    vd.release();

    LTRACE_EXIT;

    return ZX_OK;
}
