// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-devices.h>

#include <zircon/process.h>
#include <zircon/types.h>

#include <memory>

#include "magma_util/macros.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_device.h"

struct arm_mali_device {
    zx_device_t* parent_device;
    zx_device_t* zx_device;
    std::unique_ptr<MagmaDriver> magma_driver;
    std::shared_ptr<MagmaSystemDevice> magma_system_device;
};

static zx_status_t magma_start(arm_mali_device* gpu)
{
    gpu->magma_system_device = gpu->magma_driver->CreateDevice(gpu->parent_device);
    if (!gpu->magma_system_device)
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");
    return ZX_OK;
}

static zx_status_t arm_mali_open(void* context, zx_device_t** out, uint32_t flags)
{
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t arm_mali_close(void* context, uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t arm_mali_ioctl(void* context, uint32_t op, const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual)
{
    return ZX_ERR_NOT_SUPPORTED;
}

static void arm_mali_release(void* context) {}

static zx_protocol_device_t arm_mali_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = arm_mali_open,
    .close = arm_mali_close,
    .ioctl = arm_mali_ioctl,
    .release = arm_mali_release,
};

static zx_status_t arm_mali_bind(void* context, zx_device_t* parent, void** cookie)
{
    dprintf(INFO, "arm_mali_bind: binding\n");
    auto gpu = std::make_unique<arm_mali_device>();
    if (!gpu)
        return ZX_ERR_NO_MEMORY;
    gpu->parent_device = parent;
    gpu->magma_driver = MagmaDriver::Create();

    zx_status_t status = magma_start(gpu.get());
    if (status != ZX_OK)
        return status;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "arm_mali_gpu";
    args.ctx = gpu.get();
    args.ops = &arm_mali_device_proto;

    status = device_add(parent, &args, &gpu->zx_device);
    if (status != ZX_OK)
        return DRET_MSG(status, "device_add failed");

    return ZX_OK;
}

zx_driver_ops_t arm_gpu_driver_ops = {
    .version = DRIVER_OPS_VERSION, .bind = arm_mali_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(arm_gpu, arm_gpu_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ARM_MALI),
ZIRCON_DRIVER_END(arm_gpu)
    // clang-format on
