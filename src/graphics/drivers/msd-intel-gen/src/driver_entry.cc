// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/types.h>

#include <atomic>
#include <set>
#include <thread>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intelgpucore.h>

#include "magma_util/dlog.h"
#include "msd_intel_pci_device.h"
#include "platform_trace.h"
#include "platform_trace_provider.h"
#include "platform_trace_provider_with_fdio.h"
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

static zx_status_t device_fidl_query(void* context, uint64_t query_id, fidl_txn_t* transaction) {
  DLOG("device_fidl_query");
  sysdrv_device_t* device = get_device(context);

  uint64_t result;
  switch (query_id) {
    case MAGMA_QUERY_DEVICE_ID:
      result = device->magma_system_device->GetDeviceId();
      break;
    case MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED:
#if MAGMA_TEST_DRIVER
      result = 1;
#else
      result = 0;
#endif
      break;
    default:
      if (!device->magma_system_device->Query(query_id, &result))
        return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled query param 0x%" PRIx64, result);
  }
  DLOG("query query_id 0x%" PRIx64 " returning 0x%" PRIx64, query_id, result);

  zx_status_t status = fuchsia_gpu_magma_DeviceQuery_reply(transaction, result);
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceQuery_reply failed: %d", status);
  return ZX_OK;
}

static zx_status_t device_fidl_query_returns_buffer(void* context, uint64_t query_id,
                                                    fidl_txn_t* transaction) {
  DLOG("device_fidl_query_returns_buffer");
  sysdrv_device_t* device = get_device(context);

  zx_handle_t result;
  if (!device->magma_system_device->QueryReturnsBuffer(query_id, &result))
    return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled query param 0x%" PRIx64, query_id);
  DLOG("query returns buffer query_id 0x%" PRIx64 " returning 0x%x", query_id, result);

  zx_status_t status = fuchsia_gpu_magma_DeviceQueryReturnsBuffer_reply(transaction, result);
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceQueryReturnsBuffer_reply failed: %d", status);
  return ZX_OK;
}

static zx_status_t device_fidl_connect(void* context, uint64_t client_id, fidl_txn_t* transaction) {
  DLOG("magma_DeviceConnectOrdinal");
  sysdrv_device_t* device = get_device(context);

  auto connection =
      MagmaSystemDevice::Open(device->magma_system_device, client_id, /*thread_profile*/ nullptr);
  if (!connection)
    return DRET_MSG(ZX_ERR_INVALID_ARGS, "MagmaSystemDevice::Open failed");

  zx_status_t status = fuchsia_gpu_magma_DeviceConnect_reply(
      transaction, connection->GetClientEndpoint(), connection->GetClientNotificationEndpoint());
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceConnect_reply failed: %d", status);

  device->magma_system_device->StartConnectionThread(std::move(connection));
  return ZX_OK;
}

static zx_status_t device_fidl_dump_state(void* context, uint32_t dump_type) {
  DLOG("device_fidl_dump_state");
  if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                    MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE))
    return DRET_MSG(ZX_ERR_INVALID_ARGS, "Invalid dump type %d", dump_type);

  sysdrv_device_t* device = get_device(context);
  std::unique_lock<std::mutex> lock(device->magma_mutex);
  if (device->magma_system_device)
    device->magma_system_device->DumpStatus(dump_type);
  return ZX_OK;
}

static zx_status_t device_fidl_test_restart(void* context) {
#if MAGMA_TEST_DRIVER
  DLOG("device_fidl_test_restart");
  sysdrv_device_t* device = get_device(context);
  std::unique_lock<std::mutex> lock(device->magma_mutex);
  zx_status_t status = magma_stop(device);
  if (status != ZX_OK)
    return DRET_MSG(status, "magma_stop failed");
  return magma_start(device);
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

static fuchsia_gpu_magma_Device_ops_t device_fidl_ops = {
    .Query = device_fidl_query,
    .QueryReturnsBuffer = device_fidl_query_returns_buffer,
    .Connect = device_fidl_connect,
    .DumpState = device_fidl_dump_state,
    .TestRestart = device_fidl_test_restart,
};

static zx_status_t sysdrv_gpu_message(void* context, fidl_msg_t* message, fidl_txn_t* transaction) {
  return fuchsia_gpu_magma_Device_dispatch(context, transaction, message, &device_fidl_ops);
}

static void sysdrv_gpu_release(void* ctx) {
  // TODO(ZX-1170) - when testable:
  // Free context if sysdrv_display_release has already been called
  DASSERT(false);
}

static zx_protocol_device_t sysdrv_gpu_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = sysdrv_gpu_release,
    .message = sysdrv_gpu_message,
};

// implement driver object:

static zx_status_t sysdrv_bind(void* ctx, zx_device_t* zx_device) {
  DLOG("sysdrv_bind start zx_device %p", zx_device);

  // map resources and initialize the device
  auto device = std::make_unique<sysdrv_device_t>();

  zx_status_t status =
      device_get_protocol(zx_device, ZX_PROTOCOL_INTEL_GPU_CORE, &device->gpu_core_protocol);
  if (status != ZX_OK)
    return DRET_MSG(status, "device_get_protocol failed: %d", status);

  device->magma_driver = MagmaDriver::Create();
  if (!device->magma_driver)
    return DRET_MSG(ZX_ERR_INTERNAL, "MagmaDriver::Create failed");

  if (magma::PlatformTraceProvider::Get())
    magma::InitializeTraceProviderWithFdio(magma::PlatformTraceProvider::Get());

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

static constexpr zx_driver_ops_t msd_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sysdrv_bind;
  return ops;
}();

static int magma_start(sysdrv_device_t* device) {
  DLOG("magma_start");

  device->magma_system_device = device->magma_driver->CreateDevice(&device->gpu_core_protocol);
  if (!device->magma_system_device)
    return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");

  DLOG("Created device %p", device->magma_system_device.get());

  return ZX_OK;
}

#if MAGMA_TEST_DRIVER
static int magma_stop(sysdrv_device_t* device) {
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
