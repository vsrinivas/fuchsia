// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-device.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform-bus.h"

mx_status_t platform_dev_set_interface(void* ctx, pbus_interface_t* interface) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t platform_dev_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    platform_dev_t* pdev = ctx;
    platform_bus_t* bus = pdev->bus;

    if (bus->interface.ops == NULL) {
        return MX_ERR_NOT_SUPPORTED;
    }
    return pbus_interface_get_protocol(&bus->interface, proto_id, out);
}

static mx_status_t platform_dev_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, mx_handle_t* out_handle) {
    platform_dev_t* pdev = ctx;
    return platform_map_mmio(&pdev->resources, index, cache_policy, vaddr, size, out_handle);
}

static mx_status_t platform_dev_map_interrupt(void* ctx, uint32_t index, mx_handle_t* out_handle) {
    platform_dev_t* pdev = ctx;
    return platform_map_interrupt(&pdev->resources, index, out_handle);
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .set_interface = platform_dev_set_interface,
    .get_protocol = platform_dev_get_protocol,
    .map_mmio = platform_dev_map_mmio,
    .map_interrupt = platform_dev_map_interrupt,
};

static void platform_dev_release(void* ctx) {
    platform_dev_t* dev = ctx;

    platform_release_resources(&dev->resources);
    free(dev);
}

static mx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_dev_release,
};

mx_status_t platform_bus_publish_device(platform_bus_t* bus, mdi_node_ref_t* device_node) {
    uint32_t vid = bus->vid;
    uint32_t pid = bus->pid;
    uint32_t did = 0;
    uint32_t mmio_count = 0;
    uint32_t irq_count = 0;
    const char* name = NULL;
    mdi_node_ref_t  node;

    // first pass to determine DID and count resources
    mdi_each_child(device_node, &node) {
        switch (mdi_id(&node)) {
        case MDI_NAME:
            name = mdi_node_string(&node);
            break;
        case MDI_PLATFORM_DEVICE_VID:
            mdi_node_uint32(&node, &vid);
            break;
        case MDI_PLATFORM_DEVICE_PID:
            mdi_node_uint32(&node, &pid);
            break;
        case MDI_PLATFORM_DEVICE_DID:
            mdi_node_uint32(&node, &did);
            break;
        case MDI_PLATFORM_MMIOS:
            mmio_count = mdi_child_count(&node);
            break;
        case MDI_PLATFORM_IRQS:
            irq_count = mdi_array_length(&node);
            break;
        }
    }

    if (!name || !did) {
        printf("platform_bus_publish_device: missing name or did\n");
        return MX_ERR_INVALID_ARGS;
    }
    if (did == PDEV_BUS_IMPLEMENTOR_DID) {
        printf("platform_bus_publish_device: PDEV_BUS_IMPLEMENTOR_DID not allowed\n");
        return MX_ERR_INVALID_ARGS;
    }

    platform_dev_t* dev = calloc(1, sizeof(platform_dev_t) + mmio_count * sizeof(platform_mmio_t)
                                 + irq_count * sizeof(platform_irq_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }
    dev->bus = bus;

    mx_status_t status = MX_OK;
    platform_init_resources(&dev->resources, mmio_count, irq_count);
    if (mmio_count || irq_count) {
        status = platform_add_resources(bus, &dev->resources, device_node);
        if (status != MX_OK) {
            goto fail;
        }
    }

    mx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, vid},
        {BIND_PLATFORM_DEV_PID, 0, pid},
        {BIND_PLATFORM_DEV_DID, 0, did},
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &platform_dev_proto,
        .proto_id = MX_PROTOCOL_PLATFORM_DEV,
        .proto_ops = &platform_dev_proto_ops,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(bus->mxdev, &args, &dev->mxdev);

fail:
    if (status != MX_OK) {
        platform_dev_release(dev);
    }

    return status;
}
