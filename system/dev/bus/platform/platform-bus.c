// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
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
    const char* args = device_get_args(parent);
    if (!args) {
        dprintf(ERROR, "platform_bus_bind: args missing\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t vid = 0;
    uint32_t pid = 0;
    if (sscanf(args, "vid=%u,pid=%u,", &vid, &pid) != 2) {
        dprintf(ERROR, "platform_bus_bind: could not find vid or pid in args\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    platform_bus_t* bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        return  ZX_ERR_NO_MEMORY;
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

   return device_add(parent, &add_args, &bus->zxdev);
}

static zx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = platform_bus_bind,
};

ZIRCON_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus)
