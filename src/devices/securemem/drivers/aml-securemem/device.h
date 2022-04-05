// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_DEVICE_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_DEVICE_H_

#include <fidl/fuchsia.hardware.securemem/cpp/wire.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/hardware/tee/cpp/banjo.h>
#include <lib/fpromise/result.h>
#include <lib/zx/bti.h>
#include <threads.h>
#include <zircon/types.h>

#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "sysmem-secure-mem-server.h"

namespace amlogic_secure_mem {

static constexpr const char* kDeviceName = "aml-securemem";

class AmlogicSecureMemDevice;

using AmlogicSecureMemDeviceBase =
    ddk::Device<AmlogicSecureMemDevice, ddk::Messageable<fuchsia_hardware_securemem::Device>::Mixin,
                ddk::Suspendable>;

class AmlogicSecureMemDevice : public AmlogicSecureMemDeviceBase,
                               public ddk::EmptyProtocol<ZX_PROTOCOL_SECURE_MEM> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* device);

  zx_status_t Bind();

  zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkRelease() { delete this; }

  // LLCPP interface implementations
  void GetSecureMemoryPhysicalAddress(
      GetSecureMemoryPhysicalAddressRequestView request,
      GetSecureMemoryPhysicalAddressCompleter::Sync& completer) override;

  fpromise::result<zx_paddr_t, zx_status_t> GetSecureMemoryPhysicalAddress(zx::vmo secure_mem);

 private:
  explicit AmlogicSecureMemDevice(zx_device_t* device) : AmlogicSecureMemDeviceBase(device) {}

  zx_status_t CreateAndServeSysmemTee();

  fdf_dispatcher_t* fdf_dispatcher_ = nullptr;
  ddk::PDevProtocolClient pdev_proto_client_;
  ddk::SysmemProtocolClient sysmem_proto_client_;
  ddk::TeeProtocolClient tee_proto_client_;

  // Note: |bti_| must be backed by a dummy IOMMU so that the physical address will be stable every
  // time a secure memory VMO is passed to be pinned.
  zx::bti bti_;

  // Created by ddk_dispatcher_thead_.  Ownership transferred to sysmem_secure_mem_server_thread_ by
  // successful BindAsync().  We use a separate thread because llcpp doesn't provide any way to
  // force unbind other than dispatcher shutdown (client channel closing doesn't count).  Since we
  // can't shutdown the devhost's main dispatcher, we use a separate dispatcher and shutdown that
  // dispatcher when we want to unbind.
  //
  // TODO(dustingreen): llcpp should provide a way to force unbind without shutdown of the whole
  // dispatcher.
  std::optional<SysmemSecureMemServer> sysmem_secure_mem_server_;
  thrd_t sysmem_secure_mem_server_thread_ = {};
  bool is_suspend_mexec_ = false;

  // Last on purpose.
  ClosureQueue fdf_dispatcher_closure_queue_;
};

}  // namespace amlogic_secure_mem

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_DEVICE_H_
