// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TPM_DRIVERS_TPM_TPM_H_
#define SRC_DEVICES_TPM_DRIVERS_TPM_TPM_H_

#include <fuchsia/hardware/tpmimpl/llcpp/fidl.h>
#include <lib/ddk/device.h>

#include <ddktl/device.h>

#include "src/devices/tpm/drivers/tpm/commands.h"

namespace tpm {

class TpmDevice;
using DeviceType = ddk::Device<TpmDevice, ddk::Initializable, ddk::Suspendable>;

class TpmDevice : public DeviceType {
 public:
  TpmDevice(zx_device_t* parent, fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl> client)
      : DeviceType(parent), tpm_(std::move(client)) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // DDK mixins
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() { delete this; }
  void DdkSuspend(ddk::SuspendTxn txn);

 private:
  zx_status_t DoCommand(TpmCmdHeader* cmd);

  fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl> tpm_;
};

}  // namespace tpm

#endif  // SRC_DEVICES_TPM_DRIVERS_TPM_TPM_H_
