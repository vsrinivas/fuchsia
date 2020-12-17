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
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

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

class GpuDevice;

using FidlStatus = llcpp::fuchsia::gpu::magma::Status;

using DdkDeviceType = ddk::Device<GpuDevice, ddk::Messageable, ddk::Unbindable, ddk::Initializable>;

class GpuDevice : public llcpp::fuchsia::gpu::magma::Device::Interface,
                  public DdkDeviceType,
                  public ddk::EmptyProtocol<ZX_PROTOCOL_GPU> {
 public:
  explicit GpuDevice(zx_device_t* parent_device) : DdkDeviceType(parent_device) {}

  magma::Status Query(uint64_t query_id, uint64_t* result_out) {
    DLOG("GpuDevice::Query");
    DASSERT(this->magma_system_device_);

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
        if (!this->magma_system_device_->Query(query_id, result_out))
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
    DLOG("GpuDevice::QueryReturnsBuffer");

    zx_handle_t result;
    magma::Status status = this->magma_system_device_->QueryReturnsBuffer(query_id, &result);
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
    DLOG("GpuDevice::Connect");

    // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
    // coding parameters.
    std::unique_ptr<magma::PlatformHandle> thread_profile;

    {
      // These parameters permit 2ms at 250Hz, 1ms at 500Hz, ... 50us at 10kHz.
      const zx_duration_t capacity = ZX_MSEC(2);
      const zx_duration_t deadline = ZX_MSEC(4);
      const zx_duration_t period = deadline;

      zx_handle_t handle;
      zx_status_t profile_status = device_get_deadline_profile(zxdev(), capacity, deadline, period,
                                                               "magma/connection-thread", &handle);
      if (profile_status != ZX_OK) {
        DLOG("Failed to get thread profile: %d", profile_status);
      } else {
        thread_profile = magma::PlatformHandle::Create(handle);
      }
    }

    auto connection =
        MagmaSystemDevice::Open(this->magma_system_device_, client_id, std::move(thread_profile));

    if (!connection) {
      DLOG("MagmaSystemDevice::Open failed");
      _completer.Close(ZX_ERR_INTERNAL);
      return;
    }

    _completer.Reply(zx::channel(connection->GetClientEndpoint()),
                     zx::channel(connection->GetClientNotificationEndpoint()));

    this->magma_system_device_->StartConnectionThread(std::move(connection));
  }

  void DumpState(uint32_t dump_type, DumpStateCompleter::Sync& _completer) override {
    DLOG("GpuDevice::DumpState");
    if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                      MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE)) {
      DLOG("Invalid dump type %d", dump_type);
      return;
    }

    std::unique_lock<std::mutex> lock(this->magma_mutex_);
    if (this->magma_system_device_)
      this->magma_system_device_->DumpStatus(dump_type);
  }

  void TestRestart(TestRestartCompleter::Sync& _completer) override {
#if MAGMA_TEST_DRIVER
    DLOG("GpuDevice::TestRestart");
    std::unique_lock<std::mutex> lock(this->magma_mutex_);
    zx_status_t status = MagmaStop();
    if (status != ZX_OK) {
      DLOG("magma_stop failed: %d", status);
    } else {
      status = MagmaStart();
      if (status != ZX_OK) {
        DLOG("magma_start failed: %d", status);
      }
    }
#endif
  }

  void GetUnitTestStatus(GetUnitTestStatusCompleter::Sync& _completer) override {
#if MAGMA_TEST_DRIVER
    DLOG("GpuDevice::GetUnitTestStatus");
    std::unique_lock<std::mutex> lock(this->magma_mutex_);
    _completer.Reply(this->unit_test_status_);
#endif
  }

  void DdkInit(ddk::InitTxn txn);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t Init();

 private:
  zx_status_t MagmaStart();
  zx_status_t MagmaStop();

  std::unique_ptr<MagmaDriver> magma_driver_;
  std::shared_ptr<MagmaSystemDevice> magma_system_device_;
  std::mutex magma_mutex_;
#if MAGMA_TEST_DRIVER
  zx_status_t unit_test_status_ = ZX_ERR_NOT_SUPPORTED;
#endif
  zx_koid_t perf_counter_koid_ = 0;
};

zx_status_t GpuDevice::MagmaStart() {
  magma_system_device_ = magma_driver_->CreateDevice(parent());
  if (!magma_system_device_)
    return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");
  magma_system_device_->set_perf_count_access_token_id(perf_counter_koid_);
  return ZX_OK;
}

zx_status_t GpuDevice::MagmaStop() {
  magma_system_device_->Shutdown();
  magma_system_device_.reset();
  return ZX_OK;
}

void GpuDevice::DdkInit(ddk::InitTxn txn) {
  if (!magma::MagmaPerformanceCounterDevice::AddDevice(zxdev(), &perf_counter_koid_)) {
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  magma_system_device_->set_perf_count_access_token_id(perf_counter_koid_);
  txn.Reply(ZX_OK);
}

void GpuDevice::DdkUnbind(ddk::UnbindTxn txn) {
  std::unique_lock<std::mutex> lock(magma_mutex_);
  // This will tear down client connections and cause them to return errors.
  MagmaStop();
  txn.Reply();
}

zx_status_t GpuDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  if (!magma_system_device_) {
    MAGMA_LOG(WARNING, "Got message on torn-down device");
    return ZX_ERR_BAD_STATE;
  }

  DdkTransaction ddk_transaction(txn);
  llcpp::fuchsia::gpu::magma::Device::Dispatch(this, msg, &ddk_transaction);
  return ddk_transaction.Status();
}

void GpuDevice::DdkRelease() {
  MAGMA_LOG(INFO, "Starting device_release");

  delete this;
  MAGMA_LOG(INFO, "Finished device_release");
}

zx_status_t GpuDevice::Init() {
  magma_driver_ = MagmaDriver::Create();
#if MAGMA_TEST_DRIVER
  DLOG("running magma indriver test");
  unit_test_status_ = magma_indriver_test(parent());
#endif

  zx_status_t status = MagmaStart();
  if (status != ZX_OK)
    return status;

  status = DdkAdd("magma_gpu");
  if (status != ZX_OK)
    return DRET_MSG(status, "device_add failed");
  return ZX_OK;
}

static zx_status_t driver_bind(void* context, zx_device_t* parent) {
  MAGMA_LOG(INFO, "driver_bind: binding\n");
  auto gpu = std::make_unique<GpuDevice>(parent);
  if (!gpu)
    return ZX_ERR_NO_MEMORY;

  if (magma::PlatformTraceProvider::Get())
    magma::InitializeTraceProviderWithFdio(magma::PlatformTraceProvider::Get());

  zx_status_t status = gpu->Init();
  if (status != ZX_OK) {
    return status;
  }
  // DdkAdd in Init took ownership of device.
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
