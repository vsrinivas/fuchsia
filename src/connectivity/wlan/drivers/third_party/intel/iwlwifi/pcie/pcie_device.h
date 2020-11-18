// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_PCIE_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_PCIE_DEVICE_H_

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/device.h"

namespace wlan::iwlwifi {

// This class uses the DDKTL classes to manage the lifetime of a iwlwifi driver instance.
class PcieDevice : public Device {
 public:
  // ::ddk::Device implementation.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // Creates and binds PcieDevice instance. On success hands device off to device lifecycle
  // management.
  static zx_status_t Create(void* ctx, zx_device_t* parent_device, bool load_firmware);

  // Do not allow copy via constructor or operator.
  PcieDevice(const PcieDevice& device) = delete;
  PcieDevice& operator=(const PcieDevice& other) = delete;

  explicit PcieDevice(zx_device_t* parent) : Device(parent){};
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_PCIE_DEVICE_H_
