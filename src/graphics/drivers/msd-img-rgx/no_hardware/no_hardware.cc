// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "no_hardware.h"

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/fidl-utils/bind.h>

#include <memory>
#include <mutex>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <hw/reg.h>

#include "img-sys-device.h"
#include "magma_util/macros.h"
#include "no_hardware_testing.h"
#include "platform_buffer.h"
#include "platform_logger.h"
#include "sys_driver/magma_driver.h"

using FidlStatus = llcpp::fuchsia::gpu::magma::Status;

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

zx_status_t NoHardwareGpu::DdkMessage(fidl_msg_t* message, fidl_txn_t* transaction) {
  DdkTransaction ddk_transaction(transaction);
  llcpp::fuchsia::gpu::magma::Device::Dispatch(this, message, &ddk_transaction);
  return ddk_transaction.Status();
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

void NoHardwareGpu::Query2(uint64_t query_id, Query2Completer::Sync _completer) {
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
    default: {
      magma::Status status = magma_system_device_->Query(query_id, &result);
      if (!status.ok()) {
        _completer.ReplyError(static_cast<FidlStatus>(status.getFidlStatus()));
        return;
      }
    }
  }
  DLOG("query query_id 0x%" PRIx64 " returning 0x%" PRIx64, query_id, result);

  _completer.ReplySuccess(result);
}

void NoHardwareGpu::QueryReturnsBuffer(uint64_t query_id,
                                       QueryReturnsBufferCompleter::Sync _completer) {
  DLOG("NoHardwareGpu::QueryReturnsBuffer");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  zx_handle_t result;
  switch (query_id) {
    case no_hardware_testing::kDummyQueryId: {
      auto buffer = magma::PlatformBuffer::Create(4096, "query-buffer");
      if (!buffer) {
        _completer.ReplyError(FidlStatus::MEMORY_ERROR);
        return;
      }
      if (!buffer->Write(&no_hardware_testing::kDummyQueryResult, 0,
                         sizeof(no_hardware_testing::kDummyQueryResult))) {
        _completer.ReplyError(FidlStatus::INTERNAL_ERROR);
        return;
      }
      if (!buffer->duplicate_handle(&result)) {
        _completer.ReplyError(FidlStatus::INTERNAL_ERROR);
        return;
      }
      break;
    }
    default: {
      magma::Status status = magma_system_device_->QueryReturnsBuffer(query_id, &result);
      if (!status.ok()) {
        _completer.ReplyError(static_cast<FidlStatus>(status.getFidlStatus()));
        return;
      }
    }
  }
  DLOG("query query_id 0x%" PRIx64 " returning 0x%x", query_id, result);

  _completer.ReplySuccess(zx::vmo(result));
}

void NoHardwareGpu::Connect(uint64_t client_id, ConnectCompleter::Sync _completer) {
  DLOG("NoHardwareGpu::Connect");
  std::lock_guard<std::mutex> lock(magma_mutex_);

  auto connection =
      MagmaSystemDevice::Open(magma_system_device_, client_id, /*thread_profile*/ nullptr);

  if (!connection) {
    _completer.Close(ZX_ERR_INTERNAL);
    return;
  }

  _completer.Reply(zx::channel(connection->GetClientEndpoint()),
                   zx::channel(connection->GetClientNotificationEndpoint()));

  magma_system_device_->StartConnectionThread(std::move(connection));
}

void NoHardwareGpu::DumpState(uint32_t dump_type, DumpStateCompleter::Sync _completer) {
  DLOG("NoHardwareGpu::DumpState");
  std::lock_guard<std::mutex> lock(magma_mutex_);
  if (dump_type & ~(MAGMA_DUMP_TYPE_NORMAL | MAGMA_DUMP_TYPE_PERF_COUNTERS |
                    MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE)) {
    DLOG("Invalid dump type %x", dump_type);
    return;
  }

  if (magma_system_device_)
    magma_system_device_->DumpStatus(dump_type);
}

void NoHardwareGpu::TestRestart(TestRestartCompleter::Sync _completer) {
  DLOG("NoHardwareGpu::TestRestart");
  std::lock_guard<std::mutex> lock(magma_mutex_);
  StopMagma();
  if (!StartMagma()) {
    DLOG("StartMagma failed");
  }
}

void NoHardwareGpu::GetUnitTestStatus(GetUnitTestStatusCompleter::Sync _completer) {
  _completer.Reply(ZX_ERR_NOT_SUPPORTED);
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
