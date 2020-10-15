// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/llcpp/fidl.h>
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
#include <ddktl/fidl.h>

#include "magma_performance_counter_device.h"
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
zx_status_t magma_indriver_test(zx_device_t* device);
#endif

struct gpu_device;
static zx_status_t magma_start(gpu_device* gpu);
static zx_status_t magma_stop(gpu_device* gpu);

using FidlStatus = llcpp::fuchsia::gpu::magma::Status;

struct gpu_device : public llcpp::fuchsia::gpu::magma::Device::Interface {
 public:
  magma::Status Query(uint64_t query_id, uint64_t* result_out) {
    DLOG("gpu_device::Query");
    DASSERT(this->magma_system_device);

    uint64_t result;
    switch (query_id) {
      case MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED:
#if MAGMA_TEST_DRIVER
        *result_out = 1;
#else
        *result_out = 0;
#endif
        break;
      default:
        if (!this->magma_system_device->Query(query_id, result_out))
          return DRET(MAGMA_STATUS_INVALID_ARGS);
    }
    DLOG("query query_id 0x%" PRIx64 " returning 0x%" PRIx64, query_id, result);
    return MAGMA_STATUS_OK;
  }

  void Query(uint64_t query_id, QueryCompleter::Sync& _completer) override {
    uint64_t result;
    magma::Status status = Query(query_id, &result);
    if (!status.ok()) {
      _completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    _completer.Reply(result);
  }

  void Query2(uint64_t query_id, Query2Completer::Sync& _completer) override {
    uint64_t result;
    magma::Status status = Query(query_id, &result);
    if (!status.ok()) {
      _completer.ReplyError(static_cast<FidlStatus>(status.getFidlStatus()));
      return;
    }
    _completer.ReplySuccess(result);
  }

  magma::Status QueryReturnsBuffer(uint64_t query_id, zx::vmo* buffer_out) {
    DLOG("gpu_device::QueryReturnsBuffer");

    zx_handle_t result;
    magma::Status status = this->magma_system_device->QueryReturnsBuffer(query_id, &result);
    if (!status.ok())
      return DRET(status.get());

    DLOG("query extended query_id 0x%" PRIx64 " returning 0x%x", query_id, result);
    *buffer_out = zx::vmo(result);

    return MAGMA_STATUS_OK;
  }

  void QueryReturnsBuffer(uint64_t query_id,
                          QueryReturnsBufferCompleter::Sync& _completer) override {
    zx::vmo buffer;
    magma::Status status = QueryReturnsBuffer(query_id, &buffer);
    if (!status.ok()) {
      _completer.ReplyError(static_cast<FidlStatus>(status.getFidlStatus()));
      return;
    }
    _completer.ReplySuccess(std::move(buffer));
  }

  void Connect(uint64_t client_id, ConnectCompleter::Sync& _completer) override {
    DLOG("gpu_device::Connect");

    // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
    // coding parameters.
    std::unique_ptr<magma::PlatformHandle> thread_profile;

    {
      // These parameters permit 2ms at 250Hz, 1ms at 500Hz, ... 50us at 10kHz.
      const zx_duration_t capacity = ZX_MSEC(2);
      const zx_duration_t deadline = ZX_MSEC(4);
      const zx_duration_t period = deadline;

      zx_handle_t handle;
      zx_status_t profile_status = device_get_deadline_profile(
          this->zx_device, capacity, deadline, period, "magma/connection-thread", &handle);
      if (profile_status != ZX_OK) {
        DLOG("Failed to get thread profile: %d", profile_status);
      } else {
        thread_profile = magma::PlatformHandle::Create(handle);
      }
    }

    auto connection =
        MagmaSystemDevice::Open(this->magma_system_device, client_id, std::move(thread_profile));

    if (!connection) {
      DLOG("MagmaSystemDevice::Open failed");
      _completer.Close(ZX_ERR_INTERNAL);
      return;
    }

    _completer.Reply(zx::channel(connection->GetClientEndpoint()),
                     zx::channel(connection->GetClientNotificationEndpoint()));

    this->magma_system_device->StartConnectionThread(std::move(connection));
  }

  void DumpState(uint32_t dump_type, DumpStateCompleter::Sync& _completer) override {
    DLOG("gpu_device::DumpState");
    if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                      MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE)) {
      DLOG("Invalid dump type %d", dump_type);
      return;
    }

    std::unique_lock<std::mutex> lock(this->magma_mutex);
    if (this->magma_system_device)
      this->magma_system_device->DumpStatus(dump_type);
  }

  void TestRestart(TestRestartCompleter::Sync& _completer) override {
#if MAGMA_TEST_DRIVER
    DLOG("gpu_device::TestRestart");
    std::unique_lock<std::mutex> lock(this->magma_mutex);
    zx_status_t status = magma_stop(this);
    if (status != ZX_OK) {
      DLOG("magma_stop failed: %d", status);
    } else {
      status = magma_start(this);
      if (status != ZX_OK) {
        DLOG("magma_start failed: %d", status);
      }
    }
#endif
  }

  void GetUnitTestStatus(GetUnitTestStatusCompleter::Sync& _completer) override {
#if MAGMA_TEST_DRIVER
    DLOG("gpu_device::GetUnitTestStatus");
    std::unique_lock<std::mutex> lock(this->magma_mutex);
    _completer.Reply(this->unit_test_status);
#endif
  }

  zx_device_t* parent_device;
  zx_device_t* zx_device;
  std::unique_ptr<MagmaDriver> magma_driver;
  std::shared_ptr<MagmaSystemDevice> magma_system_device;
  std::mutex magma_mutex;
  zx_status_t unit_test_status = ZX_ERR_NOT_SUPPORTED;
  zx_koid_t perf_counter_koid = 0;
};

gpu_device* get_gpu_device(void* context) { return static_cast<gpu_device*>(context); }

static zx_status_t magma_start(gpu_device* gpu) {
  gpu->magma_system_device = gpu->magma_driver->CreateDevice(gpu->parent_device);
  if (!gpu->magma_system_device)
    return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");
  gpu->magma_system_device->set_perf_count_access_token_id(gpu->perf_counter_koid);
  return ZX_OK;
}

static zx_status_t magma_stop(gpu_device* gpu) {
  gpu->magma_system_device->Shutdown();
  gpu->magma_system_device.reset();
  return ZX_OK;
}

static zx_status_t device_open(void* context, zx_device_t** out, uint32_t flags) { return ZX_OK; }

static zx_status_t device_close(void* context, uint32_t flags) { return ZX_OK; }

static void device_init(void* context) {
  gpu_device* gpu = static_cast<gpu_device*>(context);
  if (!magma::MagmaPerformanceCounterDevice::AddDevice(gpu->zx_device, &gpu->perf_counter_koid)) {
    device_init_reply(gpu->zx_device, ZX_ERR_INTERNAL, nullptr);
    return;
  }

  gpu->magma_system_device->set_perf_count_access_token_id(gpu->perf_counter_koid);
  device_init_reply(gpu->zx_device, ZX_OK, nullptr);
}

static void device_unbind(void* context) {
  gpu_device* gpu = static_cast<gpu_device*>(context);
  std::unique_lock<std::mutex> lock(gpu->magma_mutex);
  // This will tear down client connections and cause them to return errors.
  magma_stop(gpu);
  device_unbind_reply(gpu->zx_device);
}

static zx_status_t device_message(void* context, fidl_incoming_msg_t* message,
                                  fidl_txn_t* transaction) {
  gpu_device* device = get_gpu_device(context);

  if (!device->magma_system_device) {
    MAGMA_LOG(WARNING, "Got message on torn-down device");
    return ZX_ERR_BAD_STATE;
  }

  DdkTransaction ddk_transaction(transaction);
  llcpp::fuchsia::gpu::magma::Device::Dispatch(device, message, &ddk_transaction);
  return ddk_transaction.Status();
}

static void device_release(void* context) {
  gpu_device* device = get_gpu_device(context);
  MAGMA_LOG(INFO, "Starting device_release");

  delete device;
  MAGMA_LOG(INFO, "Finished device_release");
}

static zx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .init = device_init,
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
  gpu->unit_test_status = magma_indriver_test(parent);
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
