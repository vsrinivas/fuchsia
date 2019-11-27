// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_DEVICE_H_

#include <zircon/types.h>

#include <memory>

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

namespace wlan {
namespace brcmfmac {

class PcieBus;

// This class implements Device for Broadcom chips that live on the PCIE bus.  This marks the design
// interface between the Zircon DDKTL device and the brcmfmac hardware-specific logic necessary to
// drive the actual chip.
class PcieDevice : public Device {
 public:
  // Static factory function for PcieDevice instances. This factory does not return an owned
  // instance, as on successful invocation the instance will have its lifecycle managed by the
  // devhost.
  static zx_status_t Create(zx_device_t* parent_device, PcieDevice** out_device);

  PcieDevice(const PcieDevice& device) = delete;
  PcieDevice& operator=(const PcieDevice& other) = delete;

  // Trampolines for DDK functions, for platforms that support them
  zx_status_t DeviceAdd(device_add_args_t* args, zx_device_t** out_device) override;
  void DeviceAsyncRemove(zx_device_t* dev) override;
  zx_status_t LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) override;

 private:
  explicit PcieDevice(zx_device_t* parent);
  ~PcieDevice();

  std::unique_ptr<PcieBus> pcie_bus_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_DEVICE_H_
