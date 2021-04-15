// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_H_

#include <lib/fake_ddk/fake_ddk.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"

namespace wlan::testing {

class FakeDdkTester : public fake_ddk::Bind {
 public:
  FakeDdkTester() : fake_ddk::Bind() {}

  wlan::iwlwifi::Device* dev() { return dev_; }
  const std::vector<wlan::iwlwifi::MacDevice*>& macdevs() { return macdevs_; }
  fake_ddk::Bind& ddk() { return *this; }

 protected:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t ret = Bind::DeviceAdd(drv, parent, args, out);
    if (ret == ZX_OK) {
      // On successful DeviceAdd() we save off the devices so that we can access them in the test
      // to take subsequent actions or to release its resources at the end of the test.
      if (parent == fake_ddk::kFakeParent) {
        // The top node for iwlwifi will be the SimDevice
        dev_ = static_cast<wlan::iwlwifi::Device*>(args->ctx);
      } else {
        // Everything else (i.e. children) will be of type MacDevice, created via
        // the WlanphyImplCreateIface() call.
        macdevs_.push_back(static_cast<wlan::iwlwifi::MacDevice*>(args->ctx));
        IWL_ERR(this, "adding %p\n", args->ctx);
      }
    }
    return ret;
  }

 private:
  wlan::iwlwifi::Device* dev_;
  std::vector<wlan::iwlwifi::MacDevice*> macdevs_;
};
}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_H_
