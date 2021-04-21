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

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"

namespace wlan {
namespace iwlwifi {

PcieDevice::PcieDevice(zx_device_t* parent, iwl_trans* iwl_trans)
    : Device(parent), drvdata_(iwl_trans) {}

PcieDevice::~PcieDevice() {
  if (drvdata_) {
    iwl_pci_release(drvdata_);
    drvdata_ = nullptr;
  }
}

zx_status_t PcieDevice::Create(zx_device_t* parent_device, bool load_firmware) {
  zx_status_t status = ZX_OK;

  iwl_trans* iwl_trans = nullptr;
  if ((status = iwl_pci_create(parent_device, &iwl_trans, load_firmware)) != ZX_OK) {
    IWL_ERR(iwl_trans, "%s() failed pci create: %s", __func__, zx_status_get_string(status));
    return status;
  }

  fbl::AllocChecker ac;
  auto device = std::unique_ptr<PcieDevice>(new (&ac) PcieDevice(parent_device, iwl_trans));
  iwl_trans = nullptr;
  if (!ac.check()) {
    IWL_ERR(ctx, "failed to allocate pcie_device (%zu bytes)", sizeof(PcieDevice));
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->DdkAdd("iwlwifi-wlanphy")) != ZX_OK) {
    IWL_ERR(device->drvdata(), "%s() failed device add: %s", __func__,
            zx_status_get_string(status));
    return status;
  }

  // The lifecycle of this object is now managed by the devhost.
  device.release();
  return ZX_OK;
}

iwl_trans* PcieDevice::drvdata() { return drvdata_; }

const iwl_trans* PcieDevice::drvdata() const { return drvdata_; }

void PcieDevice::DdkInit(::ddk::InitTxn txn) {
  zx_status_t status = ZX_OK;

  status = iwl_pci_start(drvdata(), zxdev());
  if (status != ZX_OK) {
    IWL_ERR(drvdata(), "%s() failed pci start: %s", __func__, zx_status_get_string(status));
    txn.Reply(status);
    return;
  }

  txn.Reply(ZX_OK);
  return;
}

void PcieDevice::DdkUnbind(::ddk::UnbindTxn txn) {
  IWL_INFO(this, "Unbinding pcie device\n");
  iwl_pci_unbind(drvdata());
  txn.Reply();
}

}  // namespace iwlwifi
}  // namespace wlan
