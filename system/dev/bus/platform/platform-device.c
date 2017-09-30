// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "platform-bus.h"

static zx_status_t platform_dev_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    platform_dev_t* pdev = ctx;
    platform_bus_t* bus = pdev->bus;

    if (bus->interface.ops == NULL) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return pbus_interface_get_protocol(&bus->interface, proto_id, out);
}

static zx_status_t platform_dev_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, zx_handle_t* out_handle) {
    platform_dev_t* pdev = ctx;
    return platform_map_mmio(pdev, index, cache_policy, vaddr, size, out_handle);
}

static zx_status_t platform_dev_map_interrupt(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    platform_dev_t* pdev = ctx;
    return platform_map_interrupt(pdev, index, out_handle);
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .get_protocol = platform_dev_get_protocol,
    .map_mmio = platform_dev_map_mmio,
    .map_interrupt = platform_dev_map_interrupt,
};

void platform_dev_free(platform_dev_t* dev) {
    free(dev);
}

static zx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    // Note that we do not have a release callback here because we
    // need to support re-adding platform devices when they are reenabled.
};

zx_status_t platform_device_add(platform_bus_t* bus, const pbus_dev_t* pdev, uint32_t flags) {
    platform_dev_t* dev = calloc(1, sizeof(platform_dev_t)
                                 + pdev->mmio_count * sizeof(platform_mmio_t)
                                 + pdev->irq_count * sizeof(platform_irq_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    if (flags & ~PDEV_ADD_DISABLED) {
        return ZX_ERR_INVALID_ARGS;
    }
    dev->bus = bus;
    strlcpy(dev->name, pdev->name, sizeof(dev->name));
    dev->vid = pdev->vid;
    dev->pid = pdev->pid;
    dev->did = pdev->did;

    zx_status_t status = ZX_OK;
    platform_init_resources(&dev->resources, pdev->mmio_count, pdev->irq_count);
    if (pdev->mmio_count) {
        status = platform_bus_add_mmios(bus, &dev->resources, pdev->mmios, pdev->mmio_count);
        if (status != ZX_OK) {
            goto fail;
        }
    }
    if (pdev->irq_count) {
        status = platform_bus_add_irqs(bus, &dev->resources, pdev->irqs, pdev->irq_count);
        if (status != ZX_OK) {
            goto fail;
        }
    }
    list_add_tail(&bus->devices, &dev->node);

    if ((flags & PDEV_ADD_DISABLED) == 0) {
        status = platform_device_enable(dev, true);
    }

fail:
    if (status != ZX_OK) {
        platform_dev_free(dev);
    }

    return status;
}

zx_status_t platform_device_enable(platform_dev_t* dev, bool enable) {
    zx_status_t status = ZX_OK;

    if (enable && !dev->enabled) {
        zx_device_prop_t props[] = {
            {BIND_PLATFORM_DEV_VID, 0, dev->vid},
            {BIND_PLATFORM_DEV_PID, 0, dev->pid},
            {BIND_PLATFORM_DEV_DID, 0, dev->did},
        };

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = dev->name,
            .ctx = dev,
            .ops = &platform_dev_proto,
            .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
            .proto_ops = &platform_dev_proto_ops,
            .props = props,
            .prop_count = countof(props),
        };
        // add PCI root at top level
        zx_device_t* parent = dev->bus->zxdev;
        if (dev->did == PDEV_DID_KPCI) {
            parent = device_get_parent(parent);
        }
        status = device_add(parent, &args, &dev->zxdev);
    } else if (!enable && dev->enabled) {
        device_remove(dev->zxdev);
        dev->zxdev = NULL;
    }

    if (status == ZX_OK) {
        dev->enabled = enable;
    }

    return status;
}
