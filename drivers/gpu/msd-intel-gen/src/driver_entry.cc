// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <set>
#include <thread>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-gpu-core.h>
#include <lib/zx/channel.h>
#include <zircon/process.h>
#include <zircon/types.h>

#include "magma_util/dlog.h"
#include "magma_util/platform/zircon/zircon_platform_ioctl.h"
#include "msd_intel_pci_device.h"
#include "platform_trace.h"
#include "sys_driver/magma_driver.h"

#if MAGMA_TEST_DRIVER
void magma_indriver_test(magma::PlatformPciDevice* platform_device);
#endif

struct sysdrv_device_t {
    zx_device_t* parent_device;
    zx_device_t* zx_device_gpu;

    zx_intel_gpu_core_protocol_t gpu_core_protocol;

    std::unique_ptr<MagmaDriver> magma_driver;
    std::shared_ptr<MagmaSystemDevice> magma_system_device;
    std::mutex magma_mutex;
};

static int magma_start(sysdrv_device_t* dev);

#if MAGMA_TEST_DRIVER
static int magma_stop(sysdrv_device_t* dev);
#endif

sysdrv_device_t* get_device(void* context) { return static_cast<sysdrv_device_t*>(context); }

// implement device protocol

static zx_status_t sysdrv_common_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual)
{
    sysdrv_device_t* device = get_device(ctx);

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
            sysdrv_device_t* device = get_device(ctx);
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
    }

    return result;
}

static zx_status_t sysdrv_gpu_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len, size_t* out_actual)
{
    DLOG("sysdrv_gpu_ioctl");
    zx_status_t result = sysdrv_common_ioctl(ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
    if (result == ZX_OK)
        return ZX_OK;

    if (result != ZX_ERR_NOT_SUPPORTED)
        return result;

    sysdrv_device_t* device = get_device(ctx);
    DASSERT(device->magma_system_device);

    switch (op) {
        case IOCTL_MAGMA_CONNECT: {
            DLOG("IOCTL_MAGMA_CONNECT");
            auto request = reinterpret_cast<const magma_system_connection_request*>(in_buf);
            if (!in_buf || in_len < sizeof(*request))
                return DRET(ZX_ERR_INVALID_ARGS);

            auto device_handle_out = reinterpret_cast<uint32_t*>(out_buf);
            if (!out_buf || out_len < sizeof(*device_handle_out) * 2)
                return DRET(ZX_ERR_INVALID_ARGS);

            if ((request->capabilities & MAGMA_CAPABILITY_RENDERING) == 0)
                return DRET(ZX_ERR_INVALID_ARGS);

            auto connection = MagmaSystemDevice::Open(
                device->magma_system_device, request->client_id, MAGMA_CAPABILITY_RENDERING);
            if (!connection)
                return DRET(ZX_ERR_INVALID_ARGS);

            device_handle_out[0] = connection->GetHandle();
            device_handle_out[1] = connection->GetNotificationChannel();
            *out_actual = sizeof(*device_handle_out) * 2;
            result = ZX_OK;

            device->magma_system_device->StartConnectionThread(std::move(connection));

            break;
        }

        default:
            DLOG("sysdrv_gpu_ioctl unhandled op 0x%x", op);
    }

    return result;
}

static void sysdrv_gpu_release(void* ctx)
{
    // TODO(ZX-1170) - when testable:
    // Free context if sysdrv_display_release has already been called
    DASSERT(false);
}

static zx_protocol_device_t sysdrv_gpu_device_proto = {
    .version = DEVICE_OPS_VERSION, .ioctl = sysdrv_gpu_ioctl, .release = sysdrv_gpu_release,
};

// implement driver object:

static zx_status_t sysdrv_bind(void* ctx, zx_device_t* zx_device)
{
    DLOG("sysdrv_bind start zx_device %p", zx_device);

    // map resources and initialize the device
    auto device = std::make_unique<sysdrv_device_t>();

    zx_status_t status =
        device_get_protocol(zx_device, ZX_PROTOCOL_INTEL_GPU_CORE, &device->gpu_core_protocol);
    if (status != ZX_OK)
        return DRET_MSG(status, "device_get_protocol failed: %d", status);

    if (magma::PlatformTrace::Get())
        magma::PlatformTrace::Get()->Initialize();

    device->magma_driver = MagmaDriver::Create();
    if (!device->magma_driver)
        return DRET_MSG(ZX_ERR_INTERNAL, "MagmaDriver::Create failed");

#if MAGMA_TEST_DRIVER
    DLOG("running magma indriver test");
    {
        auto platform_device = MsdIntelPciDevice::CreateShim(&device->gpu_core_protocol);
        magma_indriver_test(platform_device.get());
    }
#endif

    device->parent_device = zx_device;

    status = magma_start(device.get());
    if (status != ZX_OK)
        return DRET_MSG(status, "magma_start failed");

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "msd-intel-gen";
    args.ctx = device.get();
    args.ops = &sysdrv_gpu_device_proto;
    args.proto_id = ZX_PROTOCOL_GPU;
    args.proto_ops = nullptr;

    status = device_add(zx_device, &args, &device->zx_device_gpu);
    if (status != ZX_OK)
        return DRET_MSG(status, "gpu device_add failed: %d", status);

    device.release();

    DLOG("initialized magma system driver");

    return ZX_OK;
}

zx_driver_ops_t msd_driver_ops = {
    .version = DRIVER_OPS_VERSION, .bind = sysdrv_bind,
};

static int magma_start(sysdrv_device_t* device)
{
    DLOG("magma_start");

    device->magma_system_device = device->magma_driver->CreateDevice(&device->gpu_core_protocol);
    if (!device->magma_system_device)
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");

    DLOG("Created device %p", device->magma_system_device.get());

    return ZX_OK;
}

#if MAGMA_TEST_DRIVER
static int magma_stop(sysdrv_device_t* device)
{
    DLOG("magma_stop");

    device->magma_system_device->Shutdown();
    device->magma_system_device.reset();

    return ZX_OK;
}
#endif


// clang-format off
ZIRCON_DRIVER_BEGIN(gpu, msd_driver_ops, "magma", "0.1", 5)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_INTEL_GPU_CORE),
ZIRCON_DRIVER_END(gpu)
