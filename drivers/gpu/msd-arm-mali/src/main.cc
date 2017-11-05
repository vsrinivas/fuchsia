// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>

#include <zircon/process.h>
#include <zircon/types.h>

#include <memory>

#include "magma_util/macros.h"
#include "magma_util/platform/zircon/zircon_platform_ioctl.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_device.h"

#if MAGMA_TEST_DRIVER
void magma_indriver_test(zx_device_t* device);
#endif

struct arm_mali_device {
    zx_device_t* parent_device;
    zx_device_t* zx_device;
    std::unique_ptr<MagmaDriver> magma_driver;
    std::shared_ptr<MagmaSystemDevice> magma_system_device;
    std::mutex magma_mutex;
};

arm_mali_device* get_arm_mali_device(void* context)
{
    return static_cast<arm_mali_device*>(context);
}

static zx_status_t magma_start(arm_mali_device* gpu)
{
    gpu->magma_system_device = gpu->magma_driver->CreateDevice(gpu->parent_device);
    if (!gpu->magma_system_device)
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");
    return ZX_OK;
}

static zx_status_t magma_stop(arm_mali_device* gpu)
{
    gpu->magma_system_device->Shutdown();
    gpu->magma_system_device.reset();
    return ZX_OK;
}

static zx_status_t arm_mali_open(void* context, zx_device_t** out, uint32_t flags) { return ZX_OK; }

static zx_status_t arm_mali_close(void* context, uint32_t flags) { return ZX_OK; }

static zx_status_t arm_mali_ioctl(void* context, uint32_t op, const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual)
{
    arm_mali_device* device = get_arm_mali_device(context);

    DASSERT(device->magma_system_device);

    zx_status_t result = ZX_ERR_NOT_SUPPORTED;

    switch (op) {
        case IOCTL_MAGMA_QUERY: {
            DLOG("IOCTL_MAGMA_QUERY");
            const uint64_t* param = reinterpret_cast<const uint64_t*>(in_buf);
            if (!in_buf || in_len < sizeof(*param))
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "bad in_buf");
            uint64_t* value_out = reinterpret_cast<uint64_t*>(out_buf);
            if (!out_buf || out_len < sizeof(*value_out))
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "bad out_buf");
            switch (*param) {
                case MAGMA_QUERY_DEVICE_ID:
                    *value_out = device->magma_system_device->GetDeviceId();
                    break;
                default:
                    if (!device->magma_system_device->Query(*param, value_out))
                        return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled param 0x%" PRIx64,
                                        *value_out);
            }
            DLOG("query param 0x%" PRIx64 " returning 0x%" PRIx64, *param, *value_out);
            *out_actual = sizeof(*value_out);
            result = ZX_OK;
            break;
        }
        case IOCTL_MAGMA_CONNECT: {
            DLOG("IOCTL_MAGMA_CONNECT");
            auto request = reinterpret_cast<const magma_system_connection_request*>(in_buf);
            if (!in_buf || in_len < sizeof(*request))
                return DRET(ZX_ERR_INVALID_ARGS);

            auto device_handle_out = reinterpret_cast<uint32_t*>(out_buf);
            if (!out_buf || out_len < sizeof(*device_handle_out))
                return DRET(ZX_ERR_INVALID_ARGS);

            if (request->capabilities != MAGMA_CAPABILITY_RENDERING)
                return DRET(ZX_ERR_INVALID_ARGS);

            auto connection = MagmaSystemDevice::Open(device->magma_system_device,
                                                      request->client_id, request->capabilities);
            if (!connection)
                return DRET(ZX_ERR_INVALID_ARGS);

            *device_handle_out = connection->GetHandle();
            *out_actual = sizeof(*device_handle_out);
            result = ZX_OK;

            device->magma_system_device->StartConnectionThread(std::move(connection));

            break;
        }

        case IOCTL_MAGMA_DUMP_STATUS: {
            DLOG("IOCTL_MAGMA_DUMP_STATUS");
            std::unique_lock<std::mutex> lock(device->magma_mutex);
            if (device->magma_system_device)
                device->magma_system_device->DumpStatus();
            result = ZX_OK;
            break;
        }

#if MAGMA_TEST_DRIVER
        case IOCTL_MAGMA_TEST_RESTART: {
            DLOG("IOCTL_MAGMA_TEST_RESTART");
            std::unique_lock<std::mutex> lock(device->magma_mutex);
            result = magma_stop(device);
            if (result != ZX_OK)
                return DRET_MSG(result, "magma_stop failed");
            result = magma_start(device);
            break;
        }
#endif

        default:
            DLOG("arm_mali_ioctl unhandled op 0x%x", op);
    }

    return result;
}

static void arm_mali_release(void* context)
{
    arm_mali_device* device = get_arm_mali_device(context);
    {
        std::unique_lock<std::mutex> lock(device->magma_mutex);
        magma_stop(device);
    }

    delete device;
}

static zx_protocol_device_t arm_mali_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = arm_mali_open,
    .close = arm_mali_close,
    .ioctl = arm_mali_ioctl,
    .release = arm_mali_release,
};

static zx_status_t arm_mali_bind(void* context, zx_device_t* parent)
{
    magma::log(magma::LOG_INFO, "arm_mali_bind: binding\n");
    auto gpu = std::make_unique<arm_mali_device>();
    if (!gpu)
        return ZX_ERR_NO_MEMORY;
    gpu->parent_device = parent;

    gpu->magma_driver = MagmaDriver::Create();

#if MAGMA_TEST_DRIVER
    DLOG("running magma indriver test");
    magma_indriver_test(parent);
#endif

    zx_status_t status = magma_start(gpu.get());
    if (status != ZX_OK)
        return status;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "arm_mali_gpu";
    args.ctx = gpu.get();
    args.ops = &arm_mali_device_proto;
    args.proto_id = ZX_PROTOCOL_GPU;

    status = device_add(parent, &args, &gpu->zx_device);
    if (status != ZX_OK)
        return DRET_MSG(status, "device_add failed");

    gpu.release();
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
