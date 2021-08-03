// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCIE_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCIE_DEVICE_H_

#include <lib/ddk/device.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlanphy-impl-device.h"

namespace async {
class Loop;
}  // namespace async

namespace wlan::iwlwifi {

// This class contains the Fuchsia-specific PCIE bus initialization logic, using the DDKTL classes
// to manage the lifetime of a iwlwifi driver instance.
class PcieDevice : public WlanphyImplDevice {
 public:
  PcieDevice(const PcieDevice& device) = delete;
  PcieDevice& operator=(const PcieDevice& other) = delete;
  virtual ~PcieDevice() override;

  // Creates and binds PcieDevice instance. On success hands device off to device lifecycle
  // management.
  static zx_status_t Create(zx_device_t* parent_device);

  // Device implementation.
  iwl_trans* drvdata() override;
  const iwl_trans* drvdata() const override;
  void DdkInit(::ddk::InitTxn txn) override;
  void DdkUnbind(::ddk::UnbindTxn txn) override;

 protected:
  explicit PcieDevice(zx_device_t* parent);

  std::unique_ptr<::async::Loop> task_loop_;
  iwl_pci_dev pci_dev_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCIE_DEVICE_H_
