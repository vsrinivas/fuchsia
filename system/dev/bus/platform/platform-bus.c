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
#include <zircon/process.h>

#include "platform-bus.h"

static void platform_bus_release_mdi(platform_bus_t* bus) {
    if (bus->mdi_addr) {
        zx_vmar_unmap(zx_vmar_root_self(), bus->mdi_addr, bus->mdi_size);
        bus->mdi_addr = 0;
    }
    if (bus->mdi_handle != ZX_HANDLE_INVALID) {
        zx_handle_close(bus->mdi_handle);
        bus->mdi_handle = ZX_HANDLE_INVALID;
    }
}

static void platform_bus_add_gpios(platform_bus_t* bus) {
    mdi_node_ref_t  gpios, gpio_node, node;
    if (mdi_find_node(&bus->bus_node, MDI_PLATFORM_BUS_GPIOS, &gpios) == ZX_OK) {
        mdi_each_child(&gpios, &gpio_node) {
            uint32_t start, count, mmio_index;

            if (mdi_find_node(&gpio_node, MDI_PLATFORM_BUS_GPIOS_START, &node) != ZX_OK) {
                printf("platform_bus_add_gpios: could not find MDI_PLATFORM_BUS_GPIOS_START\n");
                continue;
            }
            mdi_node_uint32(&node, &start);
            if (mdi_find_node(&gpio_node, MDI_PLATFORM_BUS_GPIOS_COUNT, &node) != ZX_OK) {
                printf("platform_bus_add_gpios: could not find MDI_PLATFORM_BUS_GPIOS_COUNT\n");
                continue;
            }
            mdi_node_uint32(&node, &count);
            if (mdi_find_node(&gpio_node, MDI_PLATFORM_BUS_GPIOS_MMIO_INDEX, &node) != ZX_OK) {
                printf("platform_bus_add_gpios: could not find MDI_PLATFORM_BUS_GPIOS_MMIO_INDEX\n");
                continue;
            }
            mdi_node_uint32(&node, &mmio_index);

            const uint32_t* irqs = NULL;
            uint32_t irq_count = 0;
            if (mdi_find_node(&gpio_node, MDI_PLATFORM_IRQS, &node) == ZX_OK) {
                irqs = mdi_array_values(&node);
                irq_count = mdi_array_length(&node);
            }

           pbus_interface_add_gpios(&bus->interface, start, count, mmio_index, irqs, irq_count);
        }
    }
}

static void platform_bus_publish_devices(platform_bus_t* bus) {
    mdi_node_ref_t  node;
    mdi_each_child(&bus->platform_node, &node) {
        if (mdi_id(&node) == MDI_PLATFORM_DEVICE) {
            platform_bus_publish_device(bus, &node);
        }
    }
}

zx_status_t platform_bus_set_interface(void* ctx, pbus_interface_t* interface) {
    if (!interface) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_bus_t* bus = ctx;
    memcpy(&bus->interface, interface, sizeof(bus->interface));

    platform_bus_add_gpios(bus);
    platform_bus_publish_devices(bus);
    return ZX_OK;
}

static zx_status_t platform_bus_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    platform_bus_t* bus = ctx;

    if (bus->interface.ops == NULL) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return pbus_interface_get_protocol(&bus->interface, proto_id, out);
}

static zx_status_t platform_bus_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                         void** vaddr, size_t* size, zx_handle_t* out_handle) {
    platform_bus_t* bus = ctx;
    return platform_map_mmio(&bus->resources, index, cache_policy, vaddr, size, out_handle);
}

static zx_status_t platform_bus_map_interrupt(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    platform_bus_t* bus = ctx;
    return platform_map_interrupt(&bus->resources, index, out_handle);
}

zx_status_t platform_bus_device_enable(void* ctx, uint32_t vid, uint32_t pid, uint32_t did,
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

static platform_device_protocol_ops_t platform_bus_proto_ops = {
    .set_interface = platform_bus_set_interface,
    .get_protocol = platform_bus_get_protocol,
    .map_mmio = platform_bus_map_mmio,
    .map_interrupt = platform_bus_map_interrupt,
    .device_enable = platform_bus_device_enable,
};

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;

    platform_bus_release_mdi(bus);
    platform_release_resources(&bus->resources);
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

    mdi_node_ref_t  platform_node, bus_node;
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

    // count resources for bus device
    uint32_t mmio_count = 0;
    uint32_t irq_count = 0;
    bool has_platform_bus_node = false;
    if (mdi_find_node(&platform_node, MDI_PLATFORM_BUS, &bus_node) == ZX_OK) {
        has_platform_bus_node = true;

        if (mdi_find_node(&bus_node, MDI_PLATFORM_MMIOS, &node) == ZX_OK) {
            mmio_count = mdi_child_count(&node);
        }
        if (mdi_find_node(&bus_node, MDI_PLATFORM_IRQS, &node) == ZX_OK) {
            irq_count = mdi_array_length(&node);
        }
     }

    bus = calloc(1, sizeof(platform_bus_t) + mmio_count * sizeof(platform_mmio_t)
                 + irq_count * sizeof(platform_irq_t));
    if (!bus) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    // TODO(voydanoff) get resource from devmgr
    bus->resource = get_root_resource();
    bus->vid = vid;
    bus->pid = pid;
    list_initialize(&bus->devices);
    memcpy(&bus->platform_node, &platform_node, sizeof(bus->platform_node));
    memcpy(&bus->bus_node, &bus_node, sizeof(bus->bus_node));
    bus->mdi_addr = mdi_addr;
    bus->mdi_size = mdi_size;
    bus->mdi_handle = mdi_handle;

    platform_init_resources(&bus->resources, mmio_count, irq_count);
    if (mmio_count || irq_count) {
        zx_status_t status = platform_add_resources(bus, &bus->resources, &bus_node);
        if (status != ZX_OK) {
            goto fail;
        }
    }

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
        .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
        .proto_ops = &platform_bus_proto_ops,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(parent, &add_args, &bus->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    if (!has_platform_bus_node) {
        // there is no platform bus driver to wait for, so publish our devices immediately
        platform_bus_publish_devices(bus);
    }

    return ZX_OK;

fail:
    if (mdi_addr) {
        zx_vmar_unmap(zx_vmar_root_self(), mdi_addr, mdi_size);
    }
    zx_handle_close(mdi_handle);
    free(bus);

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
