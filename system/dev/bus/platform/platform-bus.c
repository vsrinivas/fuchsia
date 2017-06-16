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
#include <ddk/protocol/platform-device.h>
#include <magenta/listnode.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>

typedef struct {
    uint32_t id;
    struct {
        void* ops;
        void* ctx;
    } proto;
    list_node_t node;
} platform_protocol_t;

typedef struct {
    mx_device_t* mxdev;
    list_node_t protocols;
} platform_bus_t;

typedef struct {
    mx_device_t* mxdev;
    platform_bus_t* bus;
    uint32_t proto_id;
    void* protocol;
    mdi_node_ref_t mdi_node;
    mx_device_prop_t props[3];
} platform_dev_t;

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;
    free(bus);
}

static mx_protocol_device_t platform_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_bus_release,
};

static void platform_dev_release(void* ctx) {
    platform_dev_t* dev = ctx;
    free(dev);
}

static mx_protocol_device_t platform_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_dev_release,
};

static mx_status_t platform_dev_find_protocol(void* ctx, uint32_t proto_id, void* out) {
    platform_dev_t* pdev = ctx;
    platform_bus_t* bus = pdev->bus;

    platform_protocol_t* proto;
    list_for_every_entry(&bus->protocols, proto, platform_protocol_t, node) {
        if (proto->id == proto_id) {
            memcpy(out, &proto->proto, sizeof(proto->proto));
            return MX_OK;
        }
    }

    return MX_ERR_NOT_FOUND;
}

static mx_status_t platform_dev_register_protocol(void* ctx, uint32_t proto_id,
                                                  void* proto_ops, void* proto_ctx) {
    platform_dev_t* pdev = ctx;
    platform_bus_t* bus = pdev->bus;

    platform_protocol_t* proto;
    if ((proto = malloc(sizeof(platform_protocol_t))) == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    proto->id = proto_id;
    proto->proto.ops = proto_ops;
    proto->proto.ctx = proto_ctx;
    list_add_tail(&bus->protocols, &proto->node);
    return MX_OK;
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .find_protocol = platform_dev_find_protocol,
    .register_protocol = platform_dev_register_protocol,
};

static mx_status_t platform_bus_publish_devices(platform_bus_t* bus, mdi_node_ref_t* node) {
    mdi_node_ref_t  device_node;
    mdi_each_child(node, &device_node) {
        if (mdi_id(&device_node) != MDI_PLATFORM_DEVICE) {
            printf("platform_bus_publish_devices: unexpected node %d\n", mdi_id(&device_node));
            continue;
        }
        uint32_t vid = 0;
        uint32_t pid = 0;
        uint32_t did = 0;
        const char* name = NULL;
        mdi_node_ref_t  node;
        mdi_each_child(&device_node, &node) {
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
            default:
                break;
            }
        }

        if (!vid || !pid || !did) {
            printf("platform_bus_publish_devices: missing vid pid or did\n");
            continue;
        }

        platform_dev_t* dev = calloc(1, sizeof(platform_dev_t));
        if (!dev) {
            return MX_ERR_NO_MEMORY;
        }
        dev->bus = bus;
        memcpy(&dev->mdi_node, &device_node, sizeof(dev->mdi_node));

        mx_device_prop_t props[] = {
            {BIND_PLATFORM_DEV_VID, 0, vid},
            {BIND_PLATFORM_DEV_PID, 0, pid},
            {BIND_PLATFORM_DEV_DID, 0, did},
        };
        static_assert(countof(props) == countof(dev->props), "");
        memcpy(dev->props, props, sizeof(dev->props));

        char name_buffer[50];
        if (!name) {
            snprintf(name_buffer, sizeof(name_buffer), "pdev-%u:%u:%u\n", vid, pid, did);
            name = name_buffer;
        }

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = dev,
            .ops = &platform_dev_proto,
            .proto_id = MX_PROTOCOL_PLATFORM_DEV,
            .proto_ops = &platform_dev_proto_ops,
            .props = dev->props,
            .prop_count = countof(dev->props),
        };

        mx_status_t status = device_add(bus->mxdev, &args, &dev->mxdev);
        if (status != MX_OK) {
            printf("platform_bus_publish_devices: failed to create device for %u:%u:%u\n",
                   vid, pid, did);
            return status;
        }
    }

    return MX_OK;
}


static mx_status_t platform_bus_bind(void* ctx, mx_device_t* parent, void** cookie) {
    mx_handle_t mdi_handle = device_get_resource(parent);
    if (mdi_handle == MX_HANDLE_INVALID) {
        printf("platform_bus_bind: mdi_handle invalid\n");
        return MX_ERR_NOT_SUPPORTED;
    }

    void* addr = NULL;
    size_t size;
    mx_status_t status = mx_vmo_get_size(mdi_handle, &size);
    if (status != MX_OK) {
        printf("platform_bus_bind: mx_vmo_get_size failed %d\n", status);
        goto fail;
    }
    status = mx_vmar_map(mx_vmar_root_self(), 0, mdi_handle, 0, size, MX_VM_FLAG_PERM_READ,
                         (uintptr_t *)&addr);
    if (status != MX_OK) {
        printf("platform_bus_bind: mx_vmar_map failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t root_node;
    status = mdi_init(addr, size, &root_node);
    if (status != MX_OK) {
        printf("platform_bus_bind: mdi_init failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t  bus_node;
    if (mdi_find_node(&root_node, MDI_PLATFORM, &bus_node) != MX_OK) {
        printf("platform_bus_bind: couldn't find MDI_PLATFORM\n");
        goto fail;
    }

    platform_bus_t* bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        status = MX_ERR_NO_MEMORY;
        goto fail;
    }
    list_initialize(&bus->protocols);

    device_add_args_t add_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "platform-bus",
        .ctx = bus,
        .ops = &platform_bus_proto,
    };

    status = device_add(parent, &add_args, &bus->mxdev);
    if (status != MX_OK) {
        goto fail;
    }

    return platform_bus_publish_devices(bus, &bus_node);

fail:
    if (addr) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)addr, size);
    }
    mx_handle_close(mdi_handle);
    return status;
}

static mx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = platform_bus_bind,
};

MAGENTA_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_BUS),
MAGENTA_DRIVER_END(platform_bus)
