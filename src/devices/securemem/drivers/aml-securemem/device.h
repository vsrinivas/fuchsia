// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECUREMEM_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECUREMEM_DEVICE_H_

#include <fuchsia/hardware/securemem/llcpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/zx/bti.h>
#include <threads.h>
#include <zircon/types.h>

#include <optional>

#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <ddktl/protocol/tee.h>

#include "sysmem-secure-mem-server.h"

namespace amlogic_secure_mem {

static constexpr const char* kDeviceName = "aml-securemem";

class AmlogicSecureMemDevice;

using AmlogicSecureMemDeviceBase =
    ddk::Device<AmlogicSecureMemDevice, ddk::Messageable, ddk::SuspendableNew>;

class AmlogicSecureMemDevice : public AmlogicSecureMemDeviceBase,
                               public ::llcpp::fuchsia::hardware::securemem::Device::Interface,
                               public ddk::EmptyProtocol<ZX_PROTOCOL_SECURE_MEM> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* device);

  zx_status_t Bind();

  zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* parent);
  void DdkSuspendNew(ddk::SuspendTxn txn);
  void DdkRelease() { delete this; }

  // LLCPP interface implementations
  void GetSecureMemoryPhysicalAddress(
      zx::vmo secure_mem, GetSecureMemoryPhysicalAddressCompleter::Sync completer) override;

  fit::result<zx_paddr_t, zx_status_t> GetSecureMemoryPhysicalAddress(zx::vmo secure_mem);

 private:
  explicit AmlogicSecureMemDevice(zx_device_t* device) : AmlogicSecureMemDeviceBase(device) {}

  zx_status_t CreateAndServeSysmemTee();

  thrd_t ddk_dispatcher_thread_ = {};
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
  ClosureQueue ddk_loop_closure_queue_;
};

}  // namespace amlogic_secure_mem

#endif  // ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECUREMEM_DEVICE_H_
