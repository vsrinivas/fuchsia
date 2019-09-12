// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECURE_MEM_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECURE_MEM_DEVICE_H_

#include <fuchsia/hardware/securemem/llcpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/zx/bti.h>
#include <zircon/types.h>

#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace amlogic_secure_mem {

class AmlogicSecureMemDevice;

using AmlogicSecureMemDeviceBase = ddk::Device<AmlogicSecureMemDevice, ddk::Messageable>;

class AmlogicSecureMemDevice : public AmlogicSecureMemDeviceBase,
                               public ::llcpp::fuchsia::hardware::securemem::Device::Interface,
                               public ddk::EmptyProtocol<ZX_PROTOCOL_SECURE_MEM> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* device);

  zx_status_t Bind();

  zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* parent);
  void DdkRelease() { delete this; }

  // LLCPP interface implementations
  void GetSecureMemoryPhysicalAddress(
      zx::vmo secure_mem, GetSecureMemoryPhysicalAddressCompleter::Sync completer) override;

  fit::result<zx_paddr_t, zx_status_t> GetSecureMemoryPhysicalAddress(zx::vmo secure_mem);

 private:
  explicit AmlogicSecureMemDevice(zx_device_t* device) : AmlogicSecureMemDeviceBase(device) {}

  static constexpr const char* kDeviceName = "aml-securemem";

  pdev_protocol_t pdev_proto_ = {};
  sysmem_protocol_t sysmem_proto_ = {};

  // Note: |bti_| must be backed by a dummy IOMMU so that the physical address will be stable every
  // time a secure memory VMO is passed to be pinned.
  zx::bti bti_;
};

}  // namespace amlogic_secure_mem

#endif  // ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECURE_MEM_DEVICE_H_
