// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/pcie_device.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/alloc_checker.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"

namespace wlan {
namespace iwlwifi {

PcieDevice::PcieDevice(zx_device_t* parent) : Device(parent) { pci_dev_ = {}; }

PcieDevice::~PcieDevice() { ZX_DEBUG_ASSERT(pci_dev_.drvdata == nullptr); }

zx_status_t PcieDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  fbl::AllocChecker ac;
  auto device = std::unique_ptr<PcieDevice>(new (&ac) PcieDevice(parent_device));
  if (!ac.check()) {
    IWL_ERR(nullptr, "failed to allocate pcie_device (%zu bytes)", sizeof(PcieDevice));
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

iwl_trans* PcieDevice::drvdata() { return pci_dev_.drvdata; }

const iwl_trans* PcieDevice::drvdata() const { return pci_dev_.drvdata; }

void PcieDevice::DdkInit(::ddk::InitTxn txn) {
  const zx_status_t status = [&]() {
    zx_status_t status = ZX_OK;

    task_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    if ((status = task_loop_->StartThread("iwlwifi-worker", nullptr)) != ZX_OK) {
      IWL_ERR(iwl_trans, "Failed to create async loop thread: %s\n", zx_status_get_string(status));
      return status;
    }

    // Fill in the relevant Fuchsia-specific fields in our driver interface struct.
    pci_dev_.dev.zxdev = zxdev();
    pci_dev_.dev.task_dispatcher = task_loop_->dispatcher();

    if ((status = device_get_fragment_protocol(parent(), "pci", ZX_PROTOCOL_PCI,
                                               &pci_dev_.proto)) != ZX_OK) {
      return status;
    }

    // Perform Fuchsia-specific PCI initialization.
    if ((status = pci_get_bti(&pci_dev_.proto, /*index*/ 0, &pci_dev_.dev.bti)) != ZX_OK) {
      IWL_ERR(nullptr, "Failed to get PCI BTI: %s\n", zx_status_get_string(status));
      return status;
    }
    pcie_device_info_t pci_info = {};
    if ((status = pci_get_device_info(&pci_dev_.proto, &pci_info)) != ZX_OK) {
      return status;
    }
    uint16_t subsystem_device_id = 0;
    if ((status = pci_config_read16(&pci_dev_.proto, PCI_CFG_SUBSYSTEM_ID, &subsystem_device_id)) !=
        ZX_OK) {
      IWL_ERR(nullptr, "Failed to read PCI subsystem device ID: %s\n",
              zx_status_get_string(status));
      return status;
    }

    IWL_INFO(nullptr, "Device ID: %04x Subsystem Device ID: %04x\n", pci_info.device_id,
             subsystem_device_id);

    if ((status = iwl_drv_init()) != ZX_OK) {
      IWL_ERR(nullptr, "Failed to init driver: %s\n", zx_status_get_string(status));
      return status;
    }

    const iwl_pci_device_id* id = nullptr;
    if ((status = iwl_pci_find_device_id(pci_info.device_id, subsystem_device_id, &id)) != ZX_OK) {
      IWL_ERR(nullptr, "Failed to find PCI config: %s\n", zx_status_get_string(status));
      return status;
    }

    if ((status = iwl_pci_probe(&pci_dev_, id)) != ZX_OK) {
      IWL_ERR(nullptr, "Failed to probe PCI device: %s\n", zx_status_get_string(status));
      return status;
    }

    return ZX_OK;
  }();

  txn.Reply(status);
  return;
}

void PcieDevice::DdkUnbind(::ddk::UnbindTxn txn) {
  iwl_pci_remove(&pci_dev_);
  task_loop_->Shutdown();
  zx_handle_close(pci_dev_.dev.bti);
  txn.Reply();
}

}  // namespace iwlwifi
}  // namespace wlan
