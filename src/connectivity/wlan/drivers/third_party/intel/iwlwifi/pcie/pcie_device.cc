// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/pcie_device.h"

#include <zircon/status.h>

#include <memory>

#include <fbl/alloc_checker.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

namespace wlan {
namespace iwlwifi {

zx_status_t PcieDevice::Create(void* ctx, zx_device_t* parent_device, bool load_firmware) {
  zx_status_t status = ZX_OK;

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<PcieDevice>(&ac, parent_device);
  if (!ac.check()) {
    IWL_ERR(ctx, "failed to allocate pcie_device (%zu bytes)", sizeof(PcieDevice));
    return ZX_ERR_NO_MEMORY;
  }

  status = iwl_pci_create(ctx, parent_device, &device->iwl_trans_, load_firmware);
  if (status != ZX_OK) {
    IWL_ERR(ctx, "%s() failed pci create: %s\n", __func__, zx_status_get_string(status));
    return status;
  }

  if ((status = device->DdkAdd("iwlwifi-wlanphy", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    IWL_ERR(ctx, "%s() failed device add: %s\n", __func__, zx_status_get_string(status));
    return status;
  }

  // TODO (fxb/61963) - Move to separate thread since this function loads the firmware.
  status = iwl_pci_start(device->iwl_trans_, device->zxdev());
  if (status != ZX_OK) {
    IWL_ERR(ctx, "%s() failed pci start: %s\n", __func__, zx_status_get_string(status));
    goto fail_post_add;
  }

  device->DdkMakeVisible();
  device.release();

  return ZX_OK;

fail_post_add:
  // Lifecycle is managed by the devhost post DdkAdd.
  // Therefore, we need to rely on async remove to free the memory.
  device.release()->DdkAsyncRemove();
  return status;
}

void PcieDevice::DdkUnbind(ddk::UnbindTxn txn) {
  IWL_INFO(this, "Unbinding pcie device\n");
  iwl_pci_unbind(iwl_trans_);
  txn.Reply();
}

void PcieDevice::DdkRelease() {
  IWL_INFO(this, "Releasing pcie device\n");
  iwl_pci_release(iwl_trans_);

  delete this;
}

}  // namespace iwlwifi
}  // namespace wlan
