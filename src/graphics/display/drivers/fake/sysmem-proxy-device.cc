// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysmem-proxy-device.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/fidl-utils/bind.h>
#include <lib/sync/completion.h>
#include <lib/sysmem-version/sysmem-version.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/device/sysmem.h>

#include <memory>

#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/protocol/platform/bus.h>

namespace display {

namespace {
fuchsia_sysmem_DriverConnector_ops_t driver_connector_ops = {
    .Connect = fidl::Binder<SysmemProxyDevice>::BindMember<&SysmemProxyDevice::Connect>,
};
}  // namespace

// severity can be ERROR, WARN, INFO, DEBUG, TRACE.  See ddk/debug.h.
//
// Using ## __VA_ARGS__ instead of __VA_OPT__(,) __VA_ARGS__ for now, since
// __VA_OPT__ doesn't seem to be available yet.
#define LOG(severity, fmt, ...) \
  zxlogf(severity, "[%s:%s:%d] " fmt "\n", "display", __func__, __LINE__, ##__VA_ARGS__)

SysmemProxyDevice::SysmemProxyDevice(zx_device_t* parent_device,
                                     sysmem_driver::Driver* parent_driver)
    : DdkDeviceType2(parent_device),
      parent_driver_(parent_driver),
      loop_(&kAsyncLoopConfigNeverAttachToThread),
      in_proc_sysmem_protocol_{.ops = &sysmem_protocol_ops_, .ctx = this} {
  ZX_DEBUG_ASSERT(parent_);
  ZX_DEBUG_ASSERT(parent_driver_);
  zx_status_t status = loop_.StartThread("sysmem", &loop_thrd_);
  ZX_ASSERT(status == ZX_OK);
}

zx_status_t SysmemProxyDevice::Connect(zx_handle_t allocator_request) {
  return SysmemConnect(zx::channel(allocator_request));
}

zx_status_t SysmemProxyDevice::SysmemConnect(zx::channel allocator_request) {
  const char* kSvcPath = "/svc/fuchsia.sysmem.Allocator";
  LOG(INFO, "fdio_service_connect to service service: %s", kSvcPath);
  return fdio_service_connect(kSvcPath, allocator_request.release());
}

zx_status_t SysmemProxyDevice::SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
  ZX_ASSERT(false);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SysmemProxyDevice::SysmemRegisterSecureMem(zx::channel tee_connection) {
  ZX_ASSERT(false);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SysmemProxyDevice::SysmemUnregisterSecureMem() {
  ZX_ASSERT(false);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SysmemProxyDevice::Bind() {
  zx_status_t status = ddk::PDevProtocolClient::CreateFromDevice(parent_, &pdev_);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed device_get_protocol() ZX_PROTOCOL_PDEV - status: %d", status);
    return status;
  }

  ddk::PBusProtocolClient pbus;
  status = ddk::PBusProtocolClient::CreateFromDevice(parent_, &pbus);
  if (status != ZX_OK) {
    LOG(ERROR, "ZX_PROTOCOL_PBUS not available %d \n", status);
    return status;
  }

  status = DdkAdd(ddk::DeviceAddArgs("sysmem")
                      .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE)
                      .set_inspect_vmo(inspector_.DuplicateVmo()));
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to bind device");
    return status;
  }

  // Register the sysmem protocol with the platform bus.
  //
  // This is essentially the in-proc version of
  // fuchsia.sysmem.DriverConnector.
  //
  // We should only pbus_register_protocol() if device_add() succeeded, but if
  // pbus_register_protocol() fails, we should remove the device without it
  // ever being visible.
  // TODO(ZX-3746) Remove this after all clients have switched to using composite protocol.
  status = pbus.RegisterProtocol(ZX_PROTOCOL_SYSMEM, &in_proc_sysmem_protocol_,
                                 sizeof(in_proc_sysmem_protocol_));
  if (status != ZX_OK) {
    DdkAsyncRemove();
    return status;
  }

  return ZX_OK;
}

zx_status_t SysmemProxyDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_sysmem_DriverConnector_dispatch(this, txn, msg, &driver_connector_ops);
}

void SysmemProxyDevice::DdkUnbind(ddk::UnbindTxn txn) {
  // Ensure all tasks started before this call finish before shutting down the loop.
  async::PostTask(loop_.dispatcher(), [this]() { loop_.Quit(); });
  // JoinThreads waits for the Quit() to execute and cause the thread to exit.
  loop_.JoinThreads();
  loop_.Shutdown();
  // After this point the FIDL servers should have been shutdown and all DDK and other protocol
  // methods will error out because posting tasks to the dispatcher fails.
  txn.Reply();
}

}  // namespace display
