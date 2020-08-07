// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

const common::MacAddr kDefaultMac({0x12, 0x34, 0x56, 0x65, 0x43, 0x21});

// Verify that a query operation works on a client interface
TEST_F(SimTest, ClientIfcQuery) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kDefaultMac),
            ZX_OK);

  wlanif_query_info_t ifc_query_result;
  SCHEDULE_CALL(zx::sec(1), &SimInterface::Query, &client_ifc, &ifc_query_result);
  env_->Run();

  // Mac address returned should match the one we specified when we created the interface
  ASSERT_EQ(MAC_ARRAY_LENGTH, common::kMacAddrLen);
  EXPECT_EQ(0, memcmp(kDefaultMac.byte, ifc_query_result.mac_addr, MAC_ARRAY_LENGTH));

  EXPECT_EQ(ifc_query_result.role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Number of bands shouldn't exceed the maximum allowable
  ASSERT_LE(ifc_query_result.num_bands, (size_t)WLAN_INFO_MAX_BANDS);

  for (size_t band = 0; band < ifc_query_result.num_bands; band++) {
    wlanif_band_capabilities* band_info = &ifc_query_result.bands[band];

    // Band id should be in valid range
    EXPECT_LE(band_info->band_id, WLAN_INFO_BAND_COUNT);

    // Number of channels shouldn't exceed the maximum allowable
    ASSERT_LE(band_info->num_channels, (size_t)WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS);
  }
}

}  // namespace wlan::brcmfmac
