// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <zircon/process.h>

#include "platform-bus.h"

zx_status_t platform_bus_set_interface(void* ctx, pbus_interface_t* interface) {
    if (!interface) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_bus_t* bus = ctx;
    memcpy(&bus->interface, interface, sizeof(bus->interface));

    return ZX_OK;
}

static zx_status_t platform_bus_device_add(void* ctx, const pbus_dev_t* dev, uint32_t flags) {
    platform_bus_t* bus = ctx;
    return platform_device_add(bus, dev, flags);
}

static zx_status_t platform_bus_device_enable(void* ctx, uint32_t vid, uint32_t pid, uint32_t did,
                                              bool enable) {
    platform_bus_t* bus = ctx;
    platform_dev_t* dev;
    list_for_every_entry(&bus->devices, dev, platform_dev_t, node) {
        if (dev->vid == vid && dev->pid == pid && dev->did == did) {
            return platform_device_enable(dev, enable);
        }
    }

    return ZX_ERR_NOT_FOUND;
}

static platform_bus_protocol_ops_t platform_bus_proto_ops = {
    .set_interface = platform_bus_set_interface,
    .device_add = platform_bus_device_add,
    .device_enable = platform_bus_device_enable,
};

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;

    platform_dev_t* dev;
    list_for_every_entry(&bus->devices, dev, platform_dev_t, node) {
        platform_dev_free(dev);
    }

    free(bus);
}

static zx_protocol_device_t platform_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_bus_release,
};

static zx_status_t platform_bus_bind(void* ctx, zx_device_t* parent, void** cookie) {
    platform_bus_t* bus = NULL;

    zx_handle_t mdi_handle = device_get_resource(parent);
    if (mdi_handle == ZX_HANDLE_INVALID) {
        printf("platform_bus_bind: mdi_handle invalid\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    uintptr_t mdi_addr = 0;
    size_t mdi_size = 0;
    zx_status_t status = zx_vmo_get_size(mdi_handle, &mdi_size);
    if (status != ZX_OK) {
        printf("platform_bus_bind: zx_vmo_get_size failed %d\n", status);
        goto fail;
    }

    status = zx_vmar_map(zx_vmar_root_self(), 0, mdi_handle, 0, mdi_size,
                         ZX_VM_FLAG_PERM_READ, &mdi_addr);
    if (status != ZX_OK) {
        printf("platform_bus_bind: zx_vmar_map failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t root_node;
    status = mdi_init((void *)mdi_addr, mdi_size, &root_node);
    if (status != ZX_OK) {
        printf("platform_bus_bind: mdi_init failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t  platform_node;
    if (mdi_find_node(&root_node, MDI_PLATFORM, &platform_node) != ZX_OK) {
        printf("platform_bus_bind: couldn't find MDI_PLATFORM\n");
        goto fail;
    }

    mdi_node_ref_t  node;
    uint32_t vid, pid;
    if (mdi_find_node(&platform_node, MDI_PLATFORM_VID, &node) != ZX_OK) {
        printf("platform_bus_bind: couldn't find MDI_PLATFORM_VID\n");
        goto fail;
    }
    mdi_node_uint32(&node, &vid);
    if (mdi_find_node(&platform_node, MDI_PLATFORM_PID, &node) != ZX_OK) {
        printf("platform_bus_bind: couldn't find MDI_PLATFORM_PID\n");
        goto fail;
    }
    mdi_node_uint32(&node, &pid);

    bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    bus->resource = get_root_resource();
    bus->vid = vid;
    bus->pid = pid;
    list_initialize(&bus->devices);

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, bus->vid},
        {BIND_PLATFORM_DEV_PID, 0, bus->pid},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_BUS_IMPLEMENTOR_DID},
    };

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "platform-bus",
        .ctx = bus,
        .ops = &platform_bus_proto,
        .proto_id = ZX_PROTOCOL_PLATFORM_BUS,
        .proto_ops = &platform_bus_proto_ops,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(parent, &add_args, &bus->zxdev);

fail:
    if (mdi_addr) {
        zx_vmar_unmap(zx_vmar_root_self(), mdi_addr, mdi_size);
    }
    zx_handle_close(mdi_handle);
    if (status != ZX_OK) {
        free(bus);
    }

    return status;
}

static zx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = platform_bus_bind,
};

ZIRCON_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus)
