// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class DynamicIfTest : public SimTest {
 public:
  DynamicIfTest() = default;
  void Init();
  void CreateInterface(wlan_info_mac_role_t role);
  void DeleteInterface(wlan_info_mac_role_t role);
  uint32_t DeviceCount();

 private:
  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
  std::unique_ptr<SimInterface> client_ifc_;
  std::unique_ptr<SimInterface> ap_ifc_;
};

wlanif_impl_ifc_protocol_ops_t DynamicIfTest::sme_ops_ = {};

void DynamicIfTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void DynamicIfTest::CreateInterface(wlan_info_mac_role_t role) {
  zx_status_t status;

  if (role == WLAN_INFO_MAC_ROLE_CLIENT)
    status = SimTest::CreateInterface(role, sme_protocol_, &client_ifc_);
  else
    status = SimTest::CreateInterface(role, sme_protocol_, &ap_ifc_);
  ASSERT_EQ(status, ZX_OK);
}

void DynamicIfTest::DeleteInterface(wlan_info_mac_role_t role) {
  uint32_t iface_id;
  zx_status_t status;

  if (role == WLAN_INFO_MAC_ROLE_CLIENT)
    iface_id = client_ifc_->iface_id_;
  else
    iface_id = ap_ifc_->iface_id_;
  status = device_->WlanphyImplDestroyIface(iface_id);
  ASSERT_EQ(status, ZX_OK);
}

uint32_t DynamicIfTest::DeviceCount() { return (dev_mgr_->DevicesCount()); }

TEST_F(DynamicIfTest, CreateDestroy) {
  Init();
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  DeleteInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));

  CreateInterface(WLAN_INFO_MAC_ROLE_AP);
  DeleteInterface(WLAN_INFO_MAC_ROLE_AP);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(DynamicIfTest, DualInterfaces) {
  Init();
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  CreateInterface(WLAN_INFO_MAC_ROLE_AP);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(3));

  DeleteInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  DeleteInterface(WLAN_INFO_MAC_ROLE_AP);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

}  // namespace wlan::brcmfmac
