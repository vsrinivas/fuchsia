// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <fuchsia/hardware/intelgpucore/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
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

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "magma_util/dlog.h"
#include "msd_defs.h"
#include "msd_intel_pci_device.h"
#include "platform_trace.h"
#include "platform_trace_provider.h"
#include "platform_trace_provider_with_fdio.h"
#include "src/graphics/lib/magma/src/magma_util/platform/zircon/magma_performance_counter_device.h"
#include "src/graphics/lib/magma/src/magma_util/platform/zircon/zircon_platform_status.h"
#include "sys_driver/magma_driver.h"

#if MAGMA_TEST_DRIVER
#include "test_bind.h"  //nogncheck
#else
#include "bind.h"  //nogncheck
#endif

#if MAGMA_TEST_DRIVER
zx_status_t magma_indriver_test(magma::PlatformPciDevice* platform_device);

using DeviceType = fuchsia_gpu_magma::TestDevice;
#else
using DeviceType = fuchsia_gpu_magma::CombinedDevice;
#endif

class IntelDevice;

using DdkDeviceType =
    ddk::Device<IntelDevice, ddk::MessageableManual, ddk::Unbindable, ddk::Initializable>;

class IntelDevice : public fidl::WireServer<DeviceType>,
                    public DdkDeviceType,
                    public ddk::EmptyProtocol<ZX_PROTOCOL_GPU> {
 public:
  explicit IntelDevice(zx_device_t* parent_device) : DdkDeviceType(parent_device) {}

  template <typename T>
  bool CheckSystemDevice(T& completer) MAGMA_REQUIRES(magma_mutex_) {
    if (!magma_system_device_) {
      MAGMA_LOG(WARNING, "Got message on torn-down device");
      completer.Close(ZX_ERR_BAD_STATE);
      return false;
    }
    return true;
  }

  void Query(QueryRequestView request, QueryCompleter::Sync& _completer) override {
    DLOG("IntelDevice::Query");
    std::lock_guard lock(magma_mutex_);
    if (!CheckSystemDevice(_completer))
      return;

    zx_handle_t result_buffer = ZX_HANDLE_INVALID;
    uint64_t result = 0;

    magma::Status status =
        magma_system_device_->Query(fidl::ToUnderlying(request->query_id), &result_buffer, &result);
    if (!status.ok()) {
      _completer.ReplyError(magma::ToZxStatus(status.get()));
      return;
    }

    if (result_buffer != ZX_HANDLE_INVALID) {
      _completer.ReplySuccess(
          fuchsia_gpu_magma::wire::DeviceQueryResponse::WithBufferResult(zx::vmo(result_buffer)));
    } else {
      _completer.ReplySuccess(fuchsia_gpu_magma::wire::DeviceQueryResponse::WithSimpleResult(
          fidl::ObjectView<uint64_t>::FromExternal(&result)));
    }
  }

  void Connect2(Connect2RequestView request, Connect2Completer::Sync& _completer) override {
    DLOG("IntelDevice::Connect2");
    std::lock_guard lock(magma_mutex_);
    if (!CheckSystemDevice(_completer))
      return;

    auto connection = MagmaSystemDevice::Open(
        magma_system_device_, request->client_id,
        magma::PlatformHandle::Create(request->primary_channel.channel().release()),
        magma::PlatformHandle::Create(request->notification_channel.channel().release()));

    if (!connection) {
      DLOG("MagmaSystemDevice::Open failed");
      _completer.Close(ZX_ERR_INTERNAL);
      return;
    }

    magma_system_device_->StartConnectionThread(std::move(connection), zxdev());
  }

  void DumpState(DumpStateRequestView request, DumpStateCompleter::Sync& _completer) override {
    DLOG("IntelDevice::DumpState");
    std::lock_guard lock(magma_mutex_);
    if (!CheckSystemDevice(_completer))
      return;
    if (request->dump_type & ~MAGMA_DUMP_TYPE_NORMAL) {
      DLOG("Invalid dump type %d", request->dump_type);
      return;
    }

    if (magma_system_device_)
      magma_system_device_->DumpStatus(request->dump_type);
  }

  void GetIcdList(GetIcdListCompleter::Sync& completer) override {
    std::lock_guard lock(magma_mutex_);
    if (!CheckSystemDevice(completer))
      return;
    fidl::Arena allocator;
    std::vector<msd_icd_info_t> msd_icd_infos;
    magma_system_device_->GetIcdList(&msd_icd_infos);
    std::vector<fuchsia_gpu_magma::wire::IcdInfo> icd_infos;
    for (auto& item : msd_icd_infos) {
      fuchsia_gpu_magma::wire::IcdInfo icd_info(allocator);
      icd_info.set_component_url(allocator, fidl::StringView::FromExternal(
                                                item.component_url, strlen(item.component_url)));
      fuchsia_gpu_magma::wire::IcdFlags flags;
      if (item.support_flags & ICD_SUPPORT_FLAG_VULKAN)
        flags |= fuchsia_gpu_magma::wire::IcdFlags::kSupportsVulkan;
      if (item.support_flags & ICD_SUPPORT_FLAG_MEDIA_CODEC_FACTORY)
        flags |= fuchsia_gpu_magma::wire::IcdFlags::kSupportsMediaCodecFactory;
      icd_info.set_flags(flags);
      icd_infos.push_back(std::move(icd_info));
    }

    completer.Reply(fidl::VectorView<fuchsia_gpu_magma::wire::IcdInfo>::FromExternal(icd_infos));
  }

#if MAGMA_TEST_DRIVER
  void GetUnitTestStatus(GetUnitTestStatusCompleter::Sync& _completer) override {
    DLOG("IntelDevice::GetUnitTestStatus");
    std::lock_guard<std::mutex> lock(magma_mutex_);
    if (!CheckSystemDevice(_completer))
      return;
    _completer.Reply(unit_test_status_);
  }
#endif  // MAGMA_TEST_DRIVER

  int MagmaStart() MAGMA_REQUIRES(magma_mutex_) {
    DLOG("magma_start");

    magma_system_device_ = magma_driver_->CreateDevice(&gpu_core_protocol_);
    if (!magma_system_device_)
      return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");

    DLOG("Created device %p", magma_system_device_.get());

    return ZX_OK;
  }

  void MagmaStop() MAGMA_REQUIRES(magma_mutex_) {
    DLOG("magma_stop");

    magma_system_device_->Shutdown();
    magma_system_device_.reset();
  }

  void DdkInit(ddk::InitTxn txn);
  void DdkMessage(fidl::IncomingHeaderAndMessage&& msg, DdkTransaction& txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t Init();

 private:
  intel_gpu_core_protocol_t gpu_core_protocol_;

  std::unique_ptr<MagmaDriver> magma_driver_ MAGMA_GUARDED(magma_mutex_);
  std::shared_ptr<MagmaSystemDevice> magma_system_device_ MAGMA_GUARDED(magma_mutex_);
  std::mutex magma_mutex_;
  zx_koid_t perf_count_access_token_id_ = 0;
#if MAGMA_TEST_DRIVER
  zx_status_t unit_test_status_ = ZX_ERR_NOT_SUPPORTED;
#endif
};

void IntelDevice::DdkInit(ddk::InitTxn txn) {
  std::lock_guard<std::mutex> lock(magma_mutex_);
  if (!magma::MagmaPerformanceCounterDevice::AddDevice(zxdev(), &perf_count_access_token_id_)) {
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  magma_system_device_->set_perf_count_access_token_id(perf_count_access_token_id_);

  txn.Reply(ZX_OK);
}

void IntelDevice::DdkUnbind(ddk::UnbindTxn txn) {
  std::lock_guard<std::mutex> lock(magma_mutex_);
  // This will tear down client connections and cause them to return errors.
  MagmaStop();
  txn.Reply();
}

void IntelDevice::DdkMessage(fidl::IncomingHeaderAndMessage&& msg, DdkTransaction& txn) {
  fidl::WireDispatch<DeviceType>(this, std::move(msg), &txn);
}

void IntelDevice::DdkRelease() {
  MAGMA_LOG(INFO, "Starting device_release");

  delete this;
  MAGMA_LOG(INFO, "Finished device_release");
}

zx_status_t IntelDevice::Init() {
  ddk::IntelGpuCoreProtocolClient gpu_core_client;
  zx_status_t status =
      ddk::IntelGpuCoreProtocolClient::CreateFromDevice(parent(), &gpu_core_client);
  if (status != ZX_OK)
    return DRET_MSG(status, "device_get_protocol failed: %d", status);

  gpu_core_client.GetProto(&gpu_core_protocol_);

  std::lock_guard<std::mutex> lock(magma_mutex_);
  magma_driver_ = MagmaDriver::Create();
#if MAGMA_TEST_DRIVER
  DLOG("running magma indriver test");
  {
    auto platform_device = MsdIntelPciDevice::CreateShim(&gpu_core_protocol_);
    unit_test_status_ = magma_indriver_test(platform_device.get());
  }
#endif

  status = MagmaStart();
  if (status != ZX_OK)
    return status;

  status = DdkAdd(ddk::DeviceAddArgs("magma_gpu")
                      .set_inspect_vmo(zx::vmo(magma_driver_->DuplicateInspectVmo())));
  if (status != ZX_OK)
    return DRET_MSG(status, "device_add failed");
  return ZX_OK;
}

static zx_status_t sysdrv_bind(void* ctx, zx_device_t* parent) {
  DLOG("sysdrv_bind start zx_device %p", parent);
  auto gpu = std::make_unique<IntelDevice>(parent);
  if (!gpu)
    return ZX_ERR_NO_MEMORY;

  if (magma::PlatformTraceProvider::Get())
    magma::InitializeTraceProviderWithFdio(magma::PlatformTraceProvider::Get());

  zx_status_t status = gpu->Init();
  if (status != ZX_OK) {
    return status;
  }
  // DdkAdd in Init took ownership of device.
  (void)gpu.release();

  DLOG("initialized magma system driver");

  return ZX_OK;
}

static constexpr zx_driver_ops_t msd_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sysdrv_bind;
  return ops;
}();

ZIRCON_DRIVER(gpu, msd_driver_ops, "magma", "0.1");
