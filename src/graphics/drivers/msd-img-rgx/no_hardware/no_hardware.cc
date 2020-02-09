// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "no_hardware.h"

#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fidl-utils/bind.h>

#include <memory>
#include <mutex>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <hw/reg.h>

#include "img-sys-device.h"
#include "magma_util/macros.h"
#include "no_hardware_testing.h"
#include "platform_buffer.h"
#include "platform_logger.h"
#include "sys_driver/magma_driver.h"

namespace {
static fuchsia_gpu_magma_Device_ops_t device_fidl_ops = {
    .Query = fidl::Binder<NoHardwareGpu>::BindMember<&NoHardwareGpu::Query>,
    .QueryReturnsBuffer =
        fidl::Binder<NoHardwareGpu>::BindMember<&NoHardwareGpu::QueryReturnsBuffer>,
    .Connect = fidl::Binder<NoHardwareGpu>::BindMember<&NoHardwareGpu::Connect>,
    .DumpState = fidl::Binder<NoHardwareGpu>::BindMember<&NoHardwareGpu::DumpState>,
    .TestRestart = fidl::Binder<NoHardwareGpu>::BindMember<&NoHardwareGpu::Restart>,
};

}  // namespace
NoHardwareGpu::~NoHardwareGpu() {
  std::lock_guard<std::mutex> lock(magma_mutex_);
  StopMagma();
}

bool NoHardwareGpu::StartMagma() {
  magma_system_device_ = magma_driver_->CreateDevice(static_cast<ImgSysDevice*>(this));
  return !!magma_system_device_;
}

void NoHardwareGpu::StopMagma() {
  if (magma_system_device_) {
    magma_system_device_->Shutdown();
    magma_system_device_.reset();
  }
}

void NoHardwareGpu::DdkRelease() { delete this; }

zx_status_t NoHardwareGpu::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_gpu_magma_Device_dispatch(this, txn, msg, &device_fidl_ops);
}

zx_status_t NoHardwareGpu::PowerUp() {
  DLOG("NoHardwareGpu::PowerUp");
  return ZX_OK;
}

zx_status_t NoHardwareGpu::PowerDown() {
  DLOG("NoHardwareGpu::PowerDown()");
  return ZX_OK;
}

zx_status_t NoHardwareGpu::Bind() {
  {
    std::lock_guard<std::mutex> lock(magma_mutex_);
    magma_driver_ = MagmaDriver::Create();
    if (!magma_driver_) {
      MAGMA_LOG(WARNING, "Failed to create MagmaDriver\n");
      return ZX_ERR_INTERNAL;
    }

    if (!StartMagma()) {
      MAGMA_LOG(WARNING, "Failed to start Magma system device\n");
      return ZX_ERR_INTERNAL;
    }
  }

  return DdkAdd("msd-img-rgx-no-hardware");
}

zx_status_t NoHardwareGpu::Query(uint64_t query_id, fidl_txn_t* transaction) {
  DLOG("NoHardwareGpu::Query");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  uint64_t result;
  switch (query_id) {
    case MAGMA_QUERY_DEVICE_ID:
      result = magma_system_device_->GetDeviceId();
      break;
    case MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED:
      result = 1;
      break;
    default:
      if (!magma_system_device_->Query(query_id, &result))
        return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled query param 0x%" PRIx64, result);
  }
  DLOG("query query_id 0x%" PRIx64 " returning 0x%" PRIx64, query_id, result);

  zx_status_t status = fuchsia_gpu_magma_DeviceQuery_reply(transaction, result);
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceQuery_reply failed: %d", status);
  return ZX_OK;
}

zx_status_t NoHardwareGpu::QueryReturnsBuffer(uint64_t query_id, fidl_txn_t* transaction) {
  DLOG("NoHardwareGpu::QueryReturnsBuffer");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  zx_handle_t result;
  switch (query_id) {
    case no_hardware_testing::kDummyQueryId: {
      auto buffer = magma::PlatformBuffer::Create(4096, "query-buffer");
      if (!buffer)
        return DRET(ZX_ERR_NO_MEMORY);
      if (!buffer->Write(&no_hardware_testing::kDummyQueryResult, 0,
                         sizeof(no_hardware_testing::kDummyQueryResult)))
        return DRET(ZX_ERR_INTERNAL);
      if (!buffer->duplicate_handle(&result))
        return DRET(ZX_ERR_INTERNAL);
      break;
    }
    default:
      if (!magma_system_device_->QueryReturnsBuffer(query_id, &result))
        return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled query param 0x%" PRIx64, query_id);
  }
  DLOG("query query_id 0x%" PRIx64 " returning 0x%x", query_id, result);

  zx_status_t status = fuchsia_gpu_magma_DeviceQueryReturnsBuffer_reply(transaction, result);
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceQueryReturnsBuffer_reply failed: %d", status);
  return ZX_OK;
}

zx_status_t NoHardwareGpu::Connect(uint64_t client_id, fidl_txn_t* transaction) {
  DLOG("NoHardwareGpu::Connect");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  auto connection =
      MagmaSystemDevice::Open(magma_system_device_, client_id, /*thread_profile*/ nullptr);
  if (!connection)
    return DRET_MSG(ZX_ERR_INVALID_ARGS, "MagmaSystemDevice::Open failed");

  zx_status_t status = fuchsia_gpu_magma_DeviceConnect_reply(
      transaction, connection->GetClientEndpoint(), connection->GetClientNotificationEndpoint());
  if (status != ZX_OK)
    return DRET_MSG(ZX_ERR_INTERNAL, "magma_DeviceConnect_reply failed: %d", status);

  magma_system_device_->StartConnectionThread(std::move(connection));
  return ZX_OK;
}

zx_status_t NoHardwareGpu::DumpState(uint32_t dump_type) {
  DLOG("NoHardwareGpu::DumpState");
  std::lock_guard<std::mutex> lock(magma_mutex_);
  if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                    MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE))
    return DRET_MSG(ZX_ERR_INVALID_ARGS, "Invalid dump type %x", dump_type);

  if (magma_system_device_)
    magma_system_device_->DumpStatus(dump_type);
  return ZX_OK;
}

zx_status_t NoHardwareGpu::Restart() {
  DLOG("NoHardwareGpu::Restart");
  std::lock_guard<std::mutex> lock(magma_mutex_);
  StopMagma();
  if (!StartMagma()) {
    return DRET_MSG(ZX_ERR_INTERNAL, "StartMagma failed\n");
  }
  return ZX_OK;
}

extern "C" zx_status_t no_hardware_gpu_bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<NoHardwareGpu>(parent);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    dev.release();
  }
  return status;
}

namespace {
static constexpr zx_driver_ops_t no_hardware_gpu_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = no_hardware_gpu_bind;
  return ops;
}();
}  // namespace

// clang-format off
ZIRCON_DRIVER_BEGIN(no_hardware_gpu, no_hardware_gpu_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(no_hardware_gpu)
