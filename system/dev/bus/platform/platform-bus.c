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
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/resource.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>

typedef struct {
    mx_paddr_t base;
    size_t length;
    mx_handle_t resource;
} platform_mmio_t;

typedef struct {
    uint32_t irq;
    mx_handle_t resource;
} platform_irq_t;

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
    list_node_t children;   // child devices we published
    mx_handle_t resource;   // root resource for platform bus
} platform_bus_t;

typedef struct {
    mx_device_t* mxdev;
    platform_bus_t* bus;
    mdi_node_ref_t mdi_node;
    list_node_t node;
    platform_mmio_t* mmios;
    platform_irq_t* irqs;
    uint32_t mmio_count;
    uint32_t irq_count;
    uint8_t extra[];        // extra storage for mmios and irqs
} platform_dev_t;

static void platform_bus_release(void* ctx) {
    platform_bus_t* bus = ctx;

    platform_protocol_t* protocol;
    while ((protocol = list_remove_head_type(&bus->protocols, platform_protocol_t, node)) != NULL) {
        free(protocol);
    }
    free(bus);
}

static mx_protocol_device_t platform_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = platform_bus_release,
};

static void platform_dev_release(void* ctx) {
    platform_dev_t* dev = ctx;

    for (uint32_t i = 0; i < dev->mmio_count; i++) {
        mx_handle_close(dev->mmios[i].resource);
    }
    for (uint32_t i = 0; i < dev->irq_count; i++) {
        mx_handle_close(dev->irqs[i].resource);
    }
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

static mx_status_t platform_dev_map_mmio(void* ctx,
                                         uint32_t index,
                                         uint32_t cache_policy,
                                         void** vaddr,
                                         size_t* size,
                                         mx_handle_t* out_handle) {
    platform_dev_t* pdev = ctx;

    if (index >= pdev->mmio_count) {
        return MX_ERR_INVALID_ARGS;
    }

    platform_mmio_t* mmio = &pdev->mmios[index];
    mx_handle_t vmo_handle;
    mx_status_t status = mx_vmo_create_physical(mmio->resource, mmio->base, mmio->length,
                                                &vmo_handle);
    if (status != MX_OK) {
        printf("platform_dev_map_mmio: mx_vmo_create_physical failed %d\n", status);
        return status;
    }

    size_t vmo_size;
    status = mx_vmo_get_size(vmo_handle, &vmo_size);
    if (status != MX_OK) {
        printf("platform_dev_map_mmio: mx_vmo_get_size failed %d\n", status);
        goto fail;
    }

    status = mx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != MX_OK) {
        printf("platform_dev_map_mmio: mx_vmo_set_cache_policy failed %d\n", status);
        goto fail;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_MAP_RANGE,
                         (uintptr_t*)vaddr);
    if (status != MX_OK) {
        printf("platform_dev_map_mmio: mx_vmar_map failed %d\n", status);
        goto fail;
    }

    *size = vmo_size;
    *out_handle = vmo_handle;
    return MX_OK;

fail:
    mx_handle_close(vmo_handle);
    return status;
}

static mx_status_t platform_dev_map_interrupt(void* ctx, uint32_t index, mx_handle_t* out_handle) {
    platform_dev_t* pdev = ctx;

    if (index >= pdev->irq_count) {
        return MX_ERR_INVALID_ARGS;
    }
    platform_irq_t* irq = &pdev->irqs[index];

    *out_handle = mx_interrupt_create(irq->resource, irq->irq, MX_FLAG_REMAP_IRQ);
    return MX_OK;
}

static platform_device_protocol_ops_t platform_dev_proto_ops = {
    .find_protocol = platform_dev_find_protocol,
    .register_protocol = platform_dev_register_protocol,
    .map_mmio = platform_dev_map_mmio,
    .map_interrupt = platform_dev_map_interrupt,
};

static mx_status_t platform_bus_add_mmio(platform_bus_t* bus, mdi_node_ref_t* node,
                                         platform_mmio_t* out_mmio) {
    uint64_t base = 0;
    uint64_t length = 0;
    mdi_node_ref_t  child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_BASE_PHYS:
            mdi_node_uint64(&child, &base);
            break;
        case MDI_LENGTH:
            mdi_node_uint64(&child, &length);
            break;
        default:
            break;
        }
    }

    if (!base || !length) {
        printf("platform_bus_add_mmio: missing base or length\n");
        return MX_ERR_INVALID_ARGS;
    }

    out_mmio->base = base;
    out_mmio->length = length;
    return mx_resource_create(bus->resource, MX_RSRC_KIND_MMIO, base, base + length - 1,
                              &out_mmio->resource);
}

static mx_status_t platform_bus_add_irq(platform_bus_t* bus, mdi_node_ref_t* node,
                                        platform_irq_t* out_irq) {
    uint32_t irq = UINT32_MAX;
    mdi_node_ref_t  child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_IRQ:
            mdi_node_uint32(&child, &irq);
            break;
        default:
            break;
        }
    }

    if (irq == UINT32_MAX) {
        printf("platform_bus_add_irq: missing irq\n");
        return MX_ERR_INVALID_ARGS;
    }

    out_irq->irq = irq;
    return mx_resource_create(bus->resource, MX_RSRC_KIND_IRQ, irq, irq, &out_irq->resource);
}

static mx_status_t platform_bus_publish_device(platform_bus_t* bus, mdi_node_ref_t* device_node) {
    uint32_t vid = 0;
    uint32_t pid = 0;
    uint32_t did = 0;
    uint32_t mmio_count = 0;
    uint32_t irq_count = 0;
    const char* name = NULL;
    mdi_node_ref_t  node;

    // first pass to determine vid/pid/did and count resources
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
        case MDI_PLATFORM_DEVICE_MMIO:
            mmio_count++;
            break;
        case MDI_PLATFORM_DEVICE_IRQ:
            irq_count++;
            break;
        default:
            break;
        }
    }

    if (!name || !vid || !pid || !did) {
        printf("platform_bus_publish_devices: missing name, vid, pid or did\n");
        return MX_ERR_INVALID_ARGS;
    }

    platform_dev_t* dev = calloc(1, sizeof(platform_dev_t) + mmio_count * sizeof(platform_mmio_t)
                                 + irq_count * sizeof(platform_irq_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }
    if (mmio_count > 0) {
        dev->mmios = (platform_mmio_t *)dev->extra;
    } else {
        dev->mmios = NULL;
    }
    if (irq_count > 0) {
        dev->irqs = (platform_irq_t *)&dev->extra[mmio_count * sizeof(platform_mmio_t)];
    } else {
        dev->irqs = NULL;
    }
    dev->bus = bus;
    dev->mmio_count = mmio_count;
    dev->irq_count = irq_count;

    uint32_t mmio_index = 0;
    uint32_t irq_index = 0;
    mx_status_t status = MX_OK;

    // second pass to create resources
    mdi_each_child(device_node, &node) {
        switch (mdi_id(&node)) {
        case MDI_PLATFORM_DEVICE_MMIO:
            status = platform_bus_add_mmio(bus, &node, &dev->mmios[mmio_index++]);
            break;
        case MDI_PLATFORM_DEVICE_IRQ:
            status = platform_bus_add_irq(bus, &node, &dev->irqs[irq_index++]);
            break;
        default:
            break;
        }

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

static mx_status_t platform_bus_publish_devices(platform_bus_t* bus, mdi_node_ref_t* node) {
    mdi_node_ref_t  device_node;
    mdi_each_child(node, &device_node) {
        if (mdi_id(&device_node) != MDI_PLATFORM_DEVICE) {
            printf("platform_bus_publish_devices: unexpected node %d\n", mdi_id(&device_node));
            continue;
        }
        // TODO(voydanoff) better to just continue with next device?
        mx_status_t status = platform_bus_publish_device(bus, &device_node);
        if (status != MX_OK) {
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

    platform_bus_t* bus = NULL;
    void* mdi_addr = NULL;
    size_t size;
    mx_status_t status = mx_vmo_get_size(mdi_handle, &size);
    if (status != MX_OK) {
        printf("platform_bus_bind: mx_vmo_get_size failed %d\n", status);
        goto fail;
    }
    status = mx_vmar_map(mx_vmar_root_self(), 0, mdi_handle, 0, size, MX_VM_FLAG_PERM_READ,
                         (uintptr_t *)&mdi_addr);
    if (status != MX_OK) {
        printf("platform_bus_bind: mx_vmar_map failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t root_node;
    status = mdi_init(mdi_addr, size, &root_node);
    if (status != MX_OK) {
        printf("platform_bus_bind: mdi_init failed %d\n", status);
        goto fail;
    }

    mdi_node_ref_t  bus_node;
    if (mdi_find_node(&root_node, MDI_PLATFORM, &bus_node) != MX_OK) {
        printf("platform_bus_bind: couldn't find MDI_PLATFORM\n");
        goto fail;
    }

    bus = calloc(1, sizeof(platform_bus_t));
    if (!bus) {
        status = MX_ERR_NO_MEMORY;
        goto fail;
    }
    list_initialize(&bus->protocols);
    list_initialize(&bus->children);
    // TODO(voydanoff) get resource from devmgr
    bus->resource = get_root_resource();

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

    status = platform_bus_publish_devices(bus, &bus_node);
    // don't need MDI any more
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)mdi_addr, size);
    mx_handle_close(mdi_handle);
    return status;

fail:
    if (bus) {
        mx_handle_close(bus->resource);
        free(bus);
    }
    if (mdi_addr) {
        mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)mdi_addr, size);
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
