// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

const uint32_t kSysmemMagic = 0xAABCADBA;

typedef struct sysmem_dev {
    zx_device_t* zxdev;
    zx_handle_t bti;
    // always kSysmemMagic
    uint32_t magic;
} sysmem_dev_t;

static const pdev_device_info_t sysmem_pdev_device_info = {
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_SYSMEM,
    .bti_count = 1,
};

static zx_status_t sysmem_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    if (index >= sysmem_pdev_device_info.bti_count || out_handle == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }
    return zx_handle_duplicate(dev->bti, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

static zx_status_t sysmem_get_device_info(void* ctx, pdev_device_info_t* out_info) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    memcpy(out_info, &sysmem_pdev_device_info, sizeof(*out_info));
    return ZX_OK;
}

static zx_status_t sysmem_get_mmio(void* ctx, uint32_t index, pdev_mmio_t* mmio) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t sysmem_get_interrupt(void* ctx, uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    return ZX_ERR_NOT_SUPPORTED;
}

typedef struct generic_protocol {
    void* ops;
    void* ctx;
} generic_protocol_t;

// For now this is a placeholder, and doesn't result in child drivers under
// the ACPI driver being able to talk to sysmem.
//
// TODO(dustingreen): child drivers under ACPI will need to be able to talk
// to sysmem.
static zx_status_t sysmem_register_protocol(void* ctx, uint32_t proto_id, const void* protocol_buffer, size_t protocol_size, const platform_proxy_cb_t* proxy_cb) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    ZX_DEBUG_ASSERT(dev);
    ZX_DEBUG_ASSERT(proto_id == ZX_PROTOCOL_SYSMEM);
    ZX_DEBUG_ASSERT(protocol_buffer);
    ZX_DEBUG_ASSERT(protocol_size == sizeof(generic_protocol_t));
    ZX_DEBUG_ASSERT(protocol_size == sizeof(sysmem_protocol_t));
    ZX_DEBUG_ASSERT(proxy_cb);

    // At the moment sysmem_register_protocol() does nothing.  See function
    // level comment for TODO.
    zxlogf(ERROR, "acpi-bus: sysmem_register_protocol() intentionally ignored for now.\n");

    return ZX_OK;
}

// Partial implementation for sysmem driver to use for now.
static pdev_protocol_ops_t sysmem_pdev_proto_ops = {
    .get_mmio = sysmem_get_mmio,
    .get_interrupt = sysmem_get_interrupt,
    .get_bti = sysmem_get_bti,
    .get_device_info = sysmem_get_device_info,
};

// Partial implementation for sysmem driver to use for now.
static pbus_protocol_ops_t sysmem_pbus_proto_ops = {
    .register_protocol = sysmem_register_protocol,
};

zx_status_t sysmem_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    generic_protocol_t* proto = protocol;

    if (proto_id == ZX_PROTOCOL_PDEV) {
        proto->ops = &sysmem_pdev_proto_ops;
        proto->ctx = dev;
        return ZX_OK;
    }

    if (proto_id == ZX_PROTOCOL_PBUS) {
        proto->ops = &sysmem_pbus_proto_ops;
        proto->ctx = dev;
        return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

static void sysmem_release(void* ctx) {
    sysmem_dev_t* dev = ctx;
    ZX_DEBUG_ASSERT(dev->magic == kSysmemMagic);

    zx_handle_close(dev->bti);
    free(dev);
}

static zx_protocol_device_t sysmem_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    // The sysmem driver needs both ZX_PROTOCOL_PDEV and ZX_PROTOCOL_PBUS, so
    // we need a .get_protocol() (can't just use the .proto_id and .proto_ops
    // fields as those can only offer one protocol).
    .get_protocol = sysmem_get_protocol,
    .release = sysmem_release,
};

zx_status_t publish_sysmem(zx_handle_t bti, zx_device_t* sys_root) {
    sysmem_dev_t* dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    dev->magic = kSysmemMagic;
    dev->bti = bti;

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, sysmem_pdev_device_info.vid},
        {BIND_PLATFORM_DEV_PID, 0, sysmem_pdev_device_info.pid},
        {BIND_PLATFORM_DEV_DID, 0, sysmem_pdev_device_info.did},
    };
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sysmem",
        .ctx = dev,
        .ops = &sysmem_dev_proto,
        // The sysmem driver requires BIND_PROTOCOL ZX_PROTOCOL_PDEV, but
        // sysmem_get_protocol() lets the sysmem driver get ZX_PROTOCOL_PBUS
        // also.
        .proto_id = ZX_PROTOCOL_PDEV,
        .proto_ops = &sysmem_pdev_proto_ops,
        .props = props,
        .prop_count = countof(props),
        .proxy_args = NULL,
        .flags = 0,
    };

    // add as a child of the sysroot
    zx_status_t status = device_add(sys_root, &args, &dev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: error %d in device_add(sys/sysmem)\n", status);
        sysmem_release(dev);
        return status;
    }

    return ZX_OK;
}
