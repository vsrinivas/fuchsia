// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_PCI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_PCI_H_

//#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mac-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/pcie_device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/fake-ddk-tester.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace wlan::testing {

constexpr int test_device_id = 0x095a;
constexpr int test_subsys_device_id = 0x9e10;

class FakeDdkTesterPci : public FakeDdkTester {
 public:
  FakeDdkTesterPci() : FakeDdkTester() {
    // PCI is the only protocol of interest here.

    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[1], 1);
    fragments[0].name = "pci";
    fragments[0].protocols.emplace_back(fake_ddk::ProtocolEntry{
        .id = ZX_PROTOCOL_PCI,
        .proto = {.ops = fake_pci_.get_protocol().ops, .ctx = fake_pci_.get_protocol().ctx},
    });

    SetFragments(std::move(fragments));

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

 private:
  pci::FakePciProtocol fake_pci_;
};
}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_FAKE_DDK_TESTER_PCI_H_
