// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_H_

#include <lib/fake_ddk/fake_ddk.h>

#include <string>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"

namespace wlan::testing {

class FakeDdkTester : public fake_ddk::Bind {
 public:
  FakeDdkTester();
  ~FakeDdkTester() override;

  // Set the firmware binary to be returned by load_firmware().
  void SetFirmware(std::string firmware);

  // State accessors.
  wlan::iwlwifi::Device* dev();
  const wlan::iwlwifi::Device* dev() const;
  const std::vector<wlan::iwlwifi::MacDevice*>& macdevs() const;
  fake_ddk::Bind& ddk();
  const fake_ddk::Bind& ddk() const;
  std::string GetFirmware() const;

  // Trampoline for the load_firmware() DDK call.
  zx_status_t LoadFirmware(zx_device_t* device, const char* path, zx_handle_t* fw, size_t* size);

 protected:
  // fake_ddk overrides.
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override;

  wlan::iwlwifi::Device* dev_ = nullptr;
  std::vector<wlan::iwlwifi::MacDevice*> macdevs_;
  std::string firmware_;
};
}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_H_
