// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TEST_STUB_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TEST_STUB_DEVICE_H_

#include <zircon/types.h>

#include <memory>

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

namespace wlan {
namespace brcmfmac {

// A stub implementatkon of Device.
class StubDevice : public Device {
 public:
  StubDevice();
  ~StubDevice() override;

  // Static factory function for StubDevice instances.
  static zx_status_t Create(std::unique_ptr<StubDevice>* out_stub_device);

  // Device implementation.
  zx_status_t DeviceAdd(device_add_args_t* args, zx_device_t** out_device) override;
  void DeviceAsyncRemove(zx_device_t* dev) override;
  zx_status_t LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) override;
  zx_status_t DeviceGetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) override;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TEST_STUB_DEVICE_H_
