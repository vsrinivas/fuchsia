// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

// Verify that attempts to set the multicast promiscuous mode on a client interface are rejected
// after the interface has been deleted.
TEST_F(SimTest, SetMulticastPromiscClient) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc), ZX_OK);

  // We use a fake child device to prevent our wlanif_impl from being deleted.
  auto device = dev_mgr_->FindFirstDevByProtocolId(ZX_PROTOCOL_WLANIF_IMPL);
  ASSERT_NE(device, std::nullopt);
  zx_device_t* fake_child = nullptr;
  ASSERT_EQ(dev_mgr_->DeviceAdd(device->as_device(), nullptr, &fake_child), ZX_OK);

  EXPECT_EQ(client_ifc.SetMulticastPromisc(true), ZX_OK);
  DeleteInterface(client_ifc);
  EXPECT_NE(client_ifc.SetMulticastPromisc(true), ZX_OK);

  dev_mgr_->DeviceAsyncRemove(fake_child);
}

// Verify that attempts to set the multicast promiscuous mode on an AP interface are rejected
// after the interface has been deleted.
TEST_F(SimTest, SetMulticastPromiscAP) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface ap_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &ap_ifc), ZX_OK);

  // We use a fake child device to prevent our wlanif_impl from being deleted.
  auto device = dev_mgr_->FindFirstDevByProtocolId(ZX_PROTOCOL_WLANIF_IMPL);
  ASSERT_NE(device, std::nullopt);
  zx_device_t* fake_child = nullptr;
  ASSERT_EQ(dev_mgr_->DeviceAdd(device->as_device(), nullptr, &fake_child), ZX_OK);

  EXPECT_EQ(ap_ifc.SetMulticastPromisc(true), ZX_OK);
  DeleteInterface(ap_ifc);
  EXPECT_NE(ap_ifc.SetMulticastPromisc(true), ZX_OK);

  dev_mgr_->DeviceAsyncRemove(fake_child);
}

// Verify that attempts to stop a soft AP after the interface is deleted does not cause a failure.
TEST_F(SimTest, StopAP) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface ap_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &ap_ifc), ZX_OK);

  ap_ifc.StartSoftAp();

  // We use a fake child device to prevent our wlanif_impl from being deleted.
  auto device = dev_mgr_->FindFirstDevByProtocolId(ZX_PROTOCOL_WLANIF_IMPL);
  ASSERT_NE(device, std::nullopt);
  zx_device_t* fake_child = nullptr;
  ASSERT_EQ(dev_mgr_->DeviceAdd(device->as_device(), nullptr, &fake_child), ZX_OK);

  DeleteInterface(ap_ifc);
  // This should safely handle the deleted iface.
  ap_ifc.StopSoftAp();

  dev_mgr_->DeviceAsyncRemove(fake_child);
}

// Verify that a firmware scan result indication after the interface is stopped does
// not cause a failure.
TEST_F(SimTest, ScanResultAfterIfaceStop) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;

  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc), ZX_OK);

  client_ifc.StartScan(0, true);
  client_ifc.StopInterface();
  // The scan result will arrive after the iface is torn down.
  env_->Run(zx::sec(1));  // This should be a no-op, not a crash.

  DeleteInterface(client_ifc);
}

}  // namespace wlan::brcmfmac
