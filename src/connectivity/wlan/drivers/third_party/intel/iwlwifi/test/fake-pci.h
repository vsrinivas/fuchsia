// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_PCI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_PCI_H_

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/pcie_device.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace wlan::testing {

constexpr int test_device_id = 0x095a;
constexpr int test_subsys_device_id = 0x9e10;

class FakePcieDdkTester : public fake_ddk::Bind {
 public:
  FakePcieDdkTester() : fake_ddk::Bind() {
    // PCI is the only protocol of interest here.
    SetProtocol(ZX_PROTOCOL_PCI, &fake_pci_.get_protocol());

    // Set up the first BAR.
    fake_pci_.CreateBar(/*bar_id=*/0, /*size=*/4096);

    // Identify as the correct device.
    fake_pci_.SetDeviceInfo({.device_id = test_device_id});
    zx::unowned_vmo config = fake_pci_.GetConfigVmo();
    config->write(&test_subsys_device_id, PCI_CFG_SUBSYSTEM_ID, sizeof(test_subsys_device_id));

    // Need an IRQ of some kind. Since Intel drivers are very specific in their
    // MSI-X handling we'll keep it simple and use a legacy interrupt.
    fake_pci_.AddLegacyInterrupt();
  }

  wlan::iwlwifi::PcieDevice* dev() { return dev_; }
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
        // The top node for iwlwifi will be the PcieDevice
        dev_ = static_cast<wlan::iwlwifi::PcieDevice*>(args->ctx);
      } else {
        // Everything else (i.e. children) will be of type MacDevice, created via
        // the WlanphyImplCreateIface() call.
        macdevs_.push_back(static_cast<wlan::iwlwifi::MacDevice*>(args->ctx));
      }
    }
    return ret;
  }

 private:
  wlan::iwlwifi::PcieDevice* dev_;
  std::vector<wlan::iwlwifi::MacDevice*> macdevs_;
  pci::FakePciProtocol fake_pci_;
};
}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_PCI_H_
