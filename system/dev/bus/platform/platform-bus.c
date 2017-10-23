// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <zircon/process.h>

#include "platform-bus.h"

zx_status_t platform_bus_set_interface(void* ctx, pbus_interface_t* interface) {
    if (!interface) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_bus_t* bus = ctx;
    memcpy(&bus->interface, interface, sizeof(bus->interface));

    pbus_interface_get_protocol(&bus->interface, ZX_PROTOCOL_USB_MODE_SWITCH, &bus->ums);
    pbus_interface_get_protocol(&bus->interface, ZX_PROTOCOL_GPIO, &bus->gpio);

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

static const char* platform_bus_get_board_name(void* ctx) {
    platform_bus_t* bus = ctx;
    return bus->board_name;
}

static platform_bus_protocol_ops_t platform_bus_proto_ops = {
    .set_interface = platform_bus_set_interface,
    .device_add = platform_bus_device_add,
    .device_enable = platform_bus_device_enable,
    .get_board_name = platform_bus_get_board_name,
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

static zx_status_t sys_device_suspend(void* ctx, uint32_t flags) {
    switch (flags) {
    case DEVICE_SUSPEND_FLAG_REBOOT:
    case DEVICE_SUSPEND_FLAG_POWEROFF:
        // Kill this driver so that the IPC channel gets closed; devmgr will
        // perform a fallback that should shutdown or reboot the machine.
        exit(0);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    };
}

static zx_protocol_device_t sys_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .suspend = sys_device_suspend,
};

static zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                       const char* args, zx_handle_t rpc_channel) {
    if (!args) {
        zxlogf(ERROR, "platform_bus_create: args missing\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t vid = 0;
    uint32_t pid = 0;
    if (sscanf(args, "vid=%u,pid=%u", &vid, &pid) != 2) {
        zxlogf(ERROR, "platform_bus_create: could not find vid or pid in args\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    platform_bus_t* bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        return  ZX_ERR_NO_MEMORY;
    }

    char* board_name = strstr(args, "board=");
    if (board_name) {
        board_name += strlen("board=");
        strncpy(bus->board_name, board_name, sizeof(bus->board_name));
        bus->board_name[sizeof(bus->board_name) - 1] = 0;
        char* comma = strchr(bus->board_name, ',');
        if (comma) {
            *comma = 0;
        }
    }

    // This creates the "sys" device
    device_add_args_t self_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ops = &sys_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_status_t r = device_add(parent, &self_args, &parent);
    if (r != ZX_OK) {
        return r;
    }

    // Then we attach the platform-bus device below it
    bus->resource = get_root_resource();
    bus->vid = vid;
    bus->pid = pid;
    list_initialize(&bus->devices);

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, bus->vid},
        {BIND_PLATFORM_DEV_PID, 0, bus->pid},
    };

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "platform",
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
    .create = platform_bus_create,
};

ZIRCON_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus)
