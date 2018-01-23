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
#include "platform_trace.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_device.h"

#if MAGMA_TEST_DRIVER
void magma_indriver_test(zx_device_t* device);
#endif

struct gpu_device {
    zx_device_t* parent_device;
    zx_device_t* zx_device;
    std::unique_ptr<MagmaDriver> magma_driver;
    std::shared_ptr<MagmaSystemDevice> magma_system_device;
    std::mutex magma_mutex;
};

gpu_device* get_gpu_device(void* context) { return static_cast<gpu_device*>(context); }

static zx_status_t magma_start(gpu_device* gpu)
{
    gpu->magma_system_device = gpu->magma_driver->CreateDevice(gpu->parent_device);
    if (!gpu->magma_system_device)
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");
    return ZX_OK;
}

static zx_status_t magma_stop(gpu_device* gpu)
{
    gpu->magma_system_device->Shutdown();
    gpu->magma_system_device.reset();
    return ZX_OK;
}

static zx_status_t device_open(void* context, zx_device_t** out, uint32_t flags) { return ZX_OK; }

static zx_status_t device_close(void* context, uint32_t flags) { return ZX_OK; }

static zx_status_t device_ioctl(void* context, uint32_t op, const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len, size_t* out_actual)
{
    gpu_device* device = get_gpu_device(context);

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
            if (!out_buf || out_len < sizeof(*device_handle_out) * 2)
                return DRET(ZX_ERR_INVALID_ARGS);

            if (request->capabilities != MAGMA_CAPABILITY_RENDERING)
                return DRET(ZX_ERR_INVALID_ARGS);

            auto connection = MagmaSystemDevice::Open(device->magma_system_device,
                                                      request->client_id, request->capabilities);
            if (!connection)
                return DRET(ZX_ERR_INVALID_ARGS);

            device_handle_out[0] = connection->GetHandle();
            device_handle_out[1] = connection->GetNotificationChannel();
            *out_actual = sizeof(*device_handle_out) * 2;
            result = ZX_OK;

            device->magma_system_device->StartConnectionThread(std::move(connection));

            break;
        }

        case IOCTL_MAGMA_DUMP_STATUS: {
            DLOG("IOCTL_MAGMA_DUMP_STATUS");
            uint32_t dump_type = 0;
            if (in_buf && in_len >= sizeof(uint32_t)) {
                dump_type = *reinterpret_cast<const uint32_t*>(in_buf);
            }
            if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                              MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE)) {
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "Invalid dump type %d", dump_type);
            }
            std::unique_lock<std::mutex> lock(device->magma_mutex);
            if (device->magma_system_device)
                device->magma_system_device->DumpStatus(dump_type);
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
            DLOG("device_ioctl unhandled op 0x%x", op);
    }

    return result;
}

static void device_release(void* context)
{
    gpu_device* device = get_gpu_device(context);
    {
        std::unique_lock<std::mutex> lock(device->magma_mutex);
        magma_stop(device);
    }

    delete device;
}

static zx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = device_open,
    .close = device_close,
    .ioctl = device_ioctl,
    .release = device_release,
};

static zx_status_t driver_bind(void* context, zx_device_t* parent)
{
    magma::log(magma::LOG_INFO, "driver_bind: binding\n");
    auto gpu = std::make_unique<gpu_device>();
    if (!gpu)
        return ZX_ERR_NO_MEMORY;
    gpu->parent_device = parent;

    if (magma::PlatformTrace::Get())
        magma::PlatformTrace::Get()->Initialize();

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
    args.name = "magma_gpu";
    args.ctx = gpu.get();
    args.ops = &device_proto;
    args.proto_id = ZX_PROTOCOL_GPU;

    status = device_add(parent, &args, &gpu->zx_device);
    if (status != ZX_OK)
        return DRET_MSG(status, "device_add failed");

    gpu.release();
    return ZX_OK;
}

zx_driver_ops_t msd_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = driver_bind,
};
