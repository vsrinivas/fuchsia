// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <stdlib.h>
#include <string.h>

typedef struct cpu_trace_dev {
    zx_device_t* zxdev;
    zx_handle_t bti;
} cpu_trace_dev_t;

static const pdev_device_info_t cpu_trace_pdev_device_info = {
    .vid = PDEV_VID_INTEL,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_INTEL_CPU_TRACE,
    .bti_count = 1,
};

static zx_status_t cpu_trace_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    cpu_trace_dev_t* dev = ctx;
    if (index >= cpu_trace_pdev_device_info.bti_count || out_handle == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }
    return zx_handle_duplicate(dev->bti, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

static zx_status_t cpu_trace_get_device_info(void* ctx, pdev_device_info_t* out_info) {
    memcpy(out_info, &cpu_trace_pdev_device_info, sizeof(*out_info));
    return ZX_OK;
}

static zx_status_t cpu_trace_map_mmio(void* ctx, uint32_t index, uint32_t cache_policy,
                                      void** out_vaddr, size_t* out_size, zx_handle_t* out_handle) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t cpu_trace_map_interrupt(void* ctx, uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
    return ZX_ERR_NOT_SUPPORTED;
}

static platform_device_protocol_ops_t cpu_trace_proto_ops = {
    .map_mmio = cpu_trace_map_mmio,
    .map_interrupt = cpu_trace_map_interrupt,
    .get_bti = cpu_trace_get_bti,
    .get_device_info = cpu_trace_get_device_info,
};


static void cpu_trace_release(void* ctx) {
    cpu_trace_dev_t* dev = ctx;
    zx_handle_close(dev->bti);
    free(dev);
}

static zx_protocol_device_t cpu_trace_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = cpu_trace_release,
};

zx_status_t publish_cpu_trace(zx_handle_t bti, zx_device_t* sys_root) {
    cpu_trace_dev_t* dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    dev->bti = bti;

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, cpu_trace_pdev_device_info.vid},
        {BIND_PLATFORM_DEV_PID, 0, cpu_trace_pdev_device_info.pid},
        {BIND_PLATFORM_DEV_DID, 0, cpu_trace_pdev_device_info.did},
    };
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "cpu-trace",
        .ctx = dev,
        .ops = &cpu_trace_dev_proto,
        .proto_id = ZX_PROTOCOL_PLATFORM_DEV,
        .proto_ops = &cpu_trace_proto_ops,
        .props = props,
        .prop_count = countof(props),
        .proxy_args = NULL,
        .flags = 0,
    };

    // add as a child of the sysroot
    zx_status_t status = device_add(sys_root, &args, &dev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: error %d in device_add(sys/cpu-trace)\n", status);
        cpu_trace_release(dev);
        return status;
    }

    return ZX_OK;
}
