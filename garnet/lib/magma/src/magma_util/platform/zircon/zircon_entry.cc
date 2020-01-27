// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/c/fidl.h>
#include <zircon/process.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_handle.h"
#include "platform_logger.h"
#include "platform_trace.h"
#include "platform_trace_provider.h"
#include "platform_trace_provider_with_fdio.h"
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

static zx_status_t magma_start(gpu_device* gpu) {
  gpu->magma_system_device = gpu->magma_driver->CreateDevice(gpu->parent_device);
  if (!gpu->magma_system_device)
    return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");
  return ZX_OK;
}

static zx_status_t magma_stop(gpu_device* gpu) {
  gpu->magma_system_device->Shutdown();
  gpu->magma_system_device.reset();
  return ZX_OK;
}

static zx_status_t device_open(void* context, zx_device_t** out, uint32_t flags) { return ZX_OK; }

static zx_status_t device_close(void* context, uint32_t flags) { return ZX_OK; }

static void device_unbind(void* context) {
  gpu_device* gpu = static_cast<gpu_device*>(context);
  std::unique_lock<std::mutex> lock(gpu->magma_mutex);
  // This will tear down client connections and cause them to return errors.
  magma_stop(gpu);
  device_unbind_reply(gpu->zx_device);
}

static zx_status_t device_fidl_query(void* context, uint64_t query_id, fidl_txn_t* transaction) {
  DLOG("device_fidl_query");
  gpu_device* device = get_gpu_device(context);
  DASSERT(device->magma_system_device);

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
  gpu_device* device = get_gpu_device(context);

  zx_handle_t result;
  if (!device->magma_system_device->QueryReturnsBuffer(query_id, &result))
    return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled query param 0x%" PRIx64, query_id);
  DLOG("query extended query_id 0x%" PRIx64 " returning 0x%x", query_id, result);

  zx_status_t status = fuchsia_gpu_magma_DeviceQueryReturnsBuffer_reply(transaction, result);
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceQueryReturnsBuffer_reply failed: %d", status);
  return ZX_OK;
}

static zx_status_t device_fidl_connect(void* context, uint64_t client_id, fidl_txn_t* transaction) {
  DLOG("magma_DeviceConnectOrdinal");
  gpu_device* device = get_gpu_device(context);

  // TODO(40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  std::unique_ptr<magma::PlatformHandle> thread_profile;

  {
    // These parameters permit 2ms at 250Hz, 1ms at 500Hz, ... 50us at 10kHz.
    const zx_duration_t capacity = ZX_MSEC(2);
    const zx_duration_t deadline = ZX_MSEC(4);
    const zx_duration_t period = deadline;

    zx_handle_t handle;
    zx_status_t profile_status = device_get_deadline_profile(
        device->zx_device, capacity, deadline, period, "magma/connection-thread", &handle);
    if (profile_status != ZX_OK) {
      DLOG("Failed to get thread profile: %d", profile_status);
    } else {
      thread_profile = magma::PlatformHandle::Create(handle);
    }
  }

  auto connection =
      MagmaSystemDevice::Open(device->magma_system_device, client_id, std::move(thread_profile));
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

  gpu_device* device = get_gpu_device(context);
  std::unique_lock<std::mutex> lock(device->magma_mutex);
  if (device->magma_system_device)
    device->magma_system_device->DumpStatus(dump_type);
  return ZX_OK;
}

static zx_status_t device_fidl_test_restart(void* context) {
#if MAGMA_TEST_DRIVER
  DLOG("device_fidl_test_restart");
  gpu_device* device = get_gpu_device(context);
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

static zx_status_t device_message(void* context, fidl_msg_t* message, fidl_txn_t* transaction) {
  gpu_device* device = get_gpu_device(context);
  if (!device->magma_system_device) {
    MAGMA_LOG(WARNING, "Got message on torn-down device");
    return ZX_ERR_BAD_STATE;
  }
  return fuchsia_gpu_magma_Device_dispatch(context, transaction, message, &device_fidl_ops);
}

static void device_release(void* context) {
  gpu_device* device = get_gpu_device(context);
  MAGMA_LOG(INFO, "Starting device_release");

  delete device;
  MAGMA_LOG(INFO, "Finished device_release");
}

static zx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = device_open,
    .close = device_close,
    .unbind = device_unbind,
    .release = device_release,
    .message = device_message,
};

static zx_status_t driver_bind(void* context, zx_device_t* parent) {
  MAGMA_LOG(INFO, "driver_bind: binding\n");
  auto gpu = std::make_unique<gpu_device>();
  if (!gpu)
    return ZX_ERR_NO_MEMORY;
  gpu->parent_device = parent;

  if (magma::PlatformTraceProvider::Get())
    magma::InitializeTraceProviderWithFdio(magma::PlatformTraceProvider::Get());

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

zx_driver_ops_t msd_driver_ops = []() constexpr {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = driver_bind;
  return ops;
}
();
