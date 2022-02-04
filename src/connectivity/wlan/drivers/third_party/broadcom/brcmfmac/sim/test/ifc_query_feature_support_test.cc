// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "fuchsia/wlan/common/c/banjo.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {
namespace {

constexpr zx::duration kSimulatedClockDuration = zx::sec(10);

}  // namespace

const common::MacAddr kDefaultMac({0x12, 0x34, 0x56, 0x65, 0x43, 0x21});

// Verify that a query for MAC sublayer features support works on a client interface
TEST_F(SimTest, ClientIfcQueryMacSublayerSupport) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kDefaultMac), ZX_OK);

  mac_sublayer_support_t resp;
  env_->ScheduleNotification(std::bind(&SimInterface::QueryMacSublayerSupport, &client_ifc, &resp),
                             zx::sec(1));
  env_->Run(kSimulatedClockDuration);

  EXPECT_FALSE(resp.rate_selection_offload.supported);
  EXPECT_EQ(resp.data_plane.data_plane_type, DATA_PLANE_TYPE_ETHERNET_DEVICE);
}

// Verify that a query for security features support works on a client interface
TEST_F(SimTest, ClientIfcQuerySecuritySupport) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kDefaultMac), ZX_OK);

  security_support_t resp;
  env_->ScheduleNotification(std::bind(&SimInterface::QuerySecuritySupport, &client_ifc, &resp),
                             zx::sec(1));
  env_->Run(kSimulatedClockDuration);

  EXPECT_TRUE(resp.sae.supported);
  EXPECT_EQ(resp.sae.handler, SAE_HANDLER_SME);
  EXPECT_TRUE(resp.mfp.supported);
}

// Verify that a query for spectrum management features support works on a client interface
TEST_F(SimTest, ClientIfcQuerySpectrumManagementSupport) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kDefaultMac), ZX_OK);

  spectrum_management_support_t resp;
  env_->ScheduleNotification(
      std::bind(&SimInterface::QuerySpectrumManagementSupport, &client_ifc, &resp), zx::sec(1));
  env_->Run(kSimulatedClockDuration);

  EXPECT_TRUE(resp.dfs.supported);
}

}  // namespace wlan::brcmfmac
