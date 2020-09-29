// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/macaddr.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class BootloaderMacAddrTest : public SimTest {
 protected:
  void Init(const common::MacAddr& mac_addr);
};

// Perform a test initialization with the bootloader mac address set to a specified value
void BootloaderMacAddrTest::Init(const common::MacAddr& mac_addr) {
  ASSERT_EQ(PreInit(), ZX_OK);

  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.SetBootloaderMacAddr(mac_addr);

  ASSERT_EQ(SimTest::Init(), ZX_OK);
}

// Verify that the value from the bootloader is assigned to a client interface
TEST_F(BootloaderMacAddrTest, GetMacAddrFromBootloader) {
  const uint8_t kMacAddr[ETH_ALEN] = { 1, 2, 3, 4, 5, 6 };
  common::MacAddr mac_addr(kMacAddr);

  Init(mac_addr);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc), ZX_OK);

  // Verify that the client interface was assigned the value provided by the simulated bootloader
  common::MacAddr actual_mac_addr;
  client_ifc.GetMacAddr(&actual_mac_addr);
  ASSERT_EQ(actual_mac_addr, mac_addr);
}

}  // namespace wlan::brcmfmac
