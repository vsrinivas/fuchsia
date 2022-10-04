// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <fuchsia/hardware/intelgpucore/c/banjo.h>
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

#include <ddktl/fidl.h>

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

struct sysdrv_device_t : public fidl::WireServer<DeviceType> {
 public:
  template <typename T>
  bool CheckSystemDevice(T& completer) MAGMA_REQUIRES(magma_mutex) {
    if (!magma_system_device) {
      MAGMA_LOG(WARNING, "Got message on torn-down device");
      completer.Close(ZX_ERR_BAD_STATE);
      return false;
    }
    return true;
  }

  void Query(QueryRequestView request, QueryCompleter::Sync& _completer) override {
    DLOG("sysdrv_device_t::Query");
    std::lock_guard lock(magma_mutex);
    if (!CheckSystemDevice(_completer))
      return;

    zx_handle_t result_buffer = ZX_HANDLE_INVALID;
    uint64_t result = 0;

    magma::Status status =
        this->magma_system_device->Query(request->query_id, &result_buffer, &result);
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
    DLOG("sysdrv_device_t::Connect2");
    std::lock_guard lock(magma_mutex);
    if (!CheckSystemDevice(_completer))
      return;

    auto connection = MagmaSystemDevice::Open(
        this->magma_system_device, request->client_id,
        magma::PlatformHandle::Create(request->primary_channel.channel().release()),
        magma::PlatformHandle::Create(request->notification_channel.channel().release()));

    if (!connection) {
      DLOG("MagmaSystemDevice::Open failed");
      _completer.Close(ZX_ERR_INTERNAL);
      return;
    }

    this->magma_system_device->StartConnectionThread(std::move(connection));
  }

  void DumpState(DumpStateRequestView request, DumpStateCompleter::Sync& _completer) override {
    DLOG("sysdrv_device_t::DumpState");
    std::lock_guard lock(magma_mutex);
    if (!CheckSystemDevice(_completer))
      return;
    if (request->dump_type & ~MAGMA_DUMP_TYPE_NORMAL) {
      DLOG("Invalid dump type %d", request->dump_type);
      return;
    }

    if (this->magma_system_device)
      this->magma_system_device->DumpStatus(request->dump_type);
  }

  void GetIcdList(GetIcdListCompleter::Sync& completer) override {
    std::lock_guard lock(magma_mutex);
    if (!CheckSystemDevice(completer))
      return;
    fidl::Arena allocator;
    std::vector<msd_icd_info_t> msd_icd_infos;
    this->magma_system_device->GetIcdList(&msd_icd_infos);
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
    DLOG("sysdrv_device_t::GetUnitTestStatus");
    std::lock_guard<std::mutex> lock(magma_mutex);
    if (!CheckSystemDevice(_completer))
      return;
    _completer.Reply(this->unit_test_status);
  }
#endif  // MAGMA_TEST_DRIVER

  int MagmaStart() MAGMA_REQUIRES(magma_mutex) {
    DLOG("magma_start");

    this->magma_system_device = this->magma_driver->CreateDevice(&this->gpu_core_protocol);
    if (!this->magma_system_device)
      return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");

    DLOG("Created device %p", this->magma_system_device.get());
    this->magma_system_device->set_perf_count_access_token_id(this->perf_count_access_token_id);

    return ZX_OK;
  }

  void MagmaStop() MAGMA_REQUIRES(magma_mutex) {
    DLOG("magma_stop");

    this->magma_system_device->Shutdown();
    this->magma_system_device.reset();
  }

  void Unbind() {
    std::lock_guard lock(magma_mutex);
    MagmaStop();
    device_unbind_reply(zx_device_gpu);
  }

  zx_device_t* parent_device;
  zx_device_t* zx_device_gpu;

  intel_gpu_core_protocol_t gpu_core_protocol;

  std::unique_ptr<MagmaDriver> magma_driver MAGMA_GUARDED(magma_mutex);
  std::shared_ptr<MagmaSystemDevice> magma_system_device MAGMA_GUARDED(magma_mutex);
  std::mutex magma_mutex;
  zx_koid_t perf_count_access_token_id = 0;
#if MAGMA_TEST_DRIVER
  zx_status_t unit_test_status = ZX_ERR_NOT_SUPPORTED;
#endif
};

sysdrv_device_t* get_device(void* context) { return static_cast<sysdrv_device_t*>(context); }

static void sysdrv_gpu_init(void* context) {
  auto* gpu = static_cast<sysdrv_device_t*>(context);
  std::lock_guard lock(gpu->magma_mutex);
  if (!magma::MagmaPerformanceCounterDevice::AddDevice(gpu->zx_device_gpu,
                                                       &gpu->perf_count_access_token_id)) {
    device_init_reply(gpu->zx_device_gpu, ZX_ERR_INTERNAL, nullptr);
    return;
  }

  gpu->magma_system_device->set_perf_count_access_token_id(gpu->perf_count_access_token_id);
  device_init_reply(gpu->zx_device_gpu, ZX_OK, nullptr);
}

static zx_status_t sysdrv_gpu_message(void* context, fidl_incoming_msg_t* message,
                                      fidl_txn_t* transaction) {
  sysdrv_device_t* device = get_device(context);
  DdkTransaction ddk_transaction(transaction);
  fidl::WireDispatch<DeviceType>(
      device, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(message), &ddk_transaction);
  return ddk_transaction.Status();
}

static void sysdrv_gpu_unbind(void* context) {
  sysdrv_device_t* device = get_device(context);
  device->Unbind();
}

static void sysdrv_gpu_release(void* context) {
  sysdrv_device_t* device = get_device(context);
  delete device;
}

static zx_protocol_device_t sysdrv_gpu_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .init = sysdrv_gpu_init,
    .unbind = sysdrv_gpu_unbind,
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

  std::lock_guard lock(device->magma_mutex);
  device->magma_driver = MagmaDriver::Create();
  if (!device->magma_driver)
    return DRET_MSG(ZX_ERR_INTERNAL, "MagmaDriver::Create failed");

  if (magma::PlatformTraceProvider::Get())
    magma::InitializeTraceProviderWithFdio(magma::PlatformTraceProvider::Get());

#if MAGMA_TEST_DRIVER
  DLOG("running magma indriver test");
  {
    auto platform_device = MsdIntelPciDevice::CreateShim(&device->gpu_core_protocol);
    device->unit_test_status = magma_indriver_test(platform_device.get());
  }
#endif

  device->parent_device = zx_device;

  status = device->MagmaStart();
  if (status != ZX_OK)
    return DRET_MSG(status, "magma_start failed");

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "msd-intel-gen";
  args.flags = DEVICE_ADD_NON_BINDABLE;
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

ZIRCON_DRIVER(gpu, msd_driver_ops, "magma", "0.1");
