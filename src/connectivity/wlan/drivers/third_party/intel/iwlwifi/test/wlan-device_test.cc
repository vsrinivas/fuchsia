// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY and MAC device callback functions.

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"
}

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-function/mock-function.h>
#include <stdio.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

typedef mock_function::MockFunction<void, void*, uint32_t, const void*, size_t,
                                    const wlan_rx_info_t*>
    recv_cb_t;

// The wrapper used by wlanmac_ifc_t.recv() to call mock-up.
void recv_wrapper(void* cookie, uint32_t flags, const void* data, size_t length,
                  const wlan_rx_info_t* info) {
  auto recv = reinterpret_cast<recv_cb_t*>(cookie);
  recv->Call(cookie, flags, data, length, info);
}

class WlanDeviceTest : public SingleApTest {
 public:
  WlanDeviceTest()
      : mvmvif_sta_{
            .mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans()),
            .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
        } {
    EXPECT_EQ(ZX_OK, iwl_nvm_init(iwl_trans_get_mvm(sim_trans_.iwl_trans())));
  }
  ~WlanDeviceTest() {}

 protected:
  static constexpr zx_handle_t sme_channel_ = 73939133;  // An arbitrary value not ZX_HANDLE_INVALID
  static constexpr uint8_t kInvalidBandIdFillByte = 0xa5;
  static constexpr wlan_info_band_t kInvalidBandId = 0xa5a5a5a5;
  struct iwl_mvm_vif mvmvif_sta_;  // The mvm_vif settings for station role.
};

//////////////////////////////////// Helper Functions  /////////////////////////////////////////////
TEST_F(WlanDeviceTest, ComposeBandList) {
  struct iwl_nvm_data nvm_data;
  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT];

  // nothing enabled
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  EXPECT_EQ(0, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(kInvalidBandId, bands[0]);
  EXPECT_EQ(kInvalidBandId, bands[1]);

  // 2.4GHz only
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_24ghz_enable = true;
  EXPECT_EQ(1, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(WLAN_INFO_BAND_2GHZ, bands[0]);
  EXPECT_EQ(kInvalidBandId, bands[1]);

  // 5GHz only
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_52ghz_enable = true;
  EXPECT_EQ(1, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(WLAN_INFO_BAND_5GHZ, bands[0]);
  EXPECT_EQ(kInvalidBandId, bands[1]);

  // both bands enabled
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_24ghz_enable = true;
  nvm_data.sku_cap_band_52ghz_enable = true;
  EXPECT_EQ(2, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(WLAN_INFO_BAND_2GHZ, bands[0]);
  EXPECT_EQ(WLAN_INFO_BAND_5GHZ, bands[1]);
}

// Short-cut to access the iwl_cfg80211_rates[] structure and convert it to 802.11 rate.
//
// Args:
//   index: the index of iwl_cfg80211_rates[].
//
// Returns:
//   the 802.11 rate.
//
static unsigned expected_rate(size_t index) {
  return cfg_rates_to_80211(iwl_cfg80211_rates[index]);
}

TEST_F(WlanDeviceTest, FillBandInfos) {
  // The default 'nvm_data' is loaded from test/sim-default-nvm.cc.

  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT] = {
      WLAN_INFO_BAND_2GHZ,
      WLAN_INFO_BAND_5GHZ,
  };
  wlan_info_band_info_t band_infos[WLAN_INFO_BAND_COUNT] = {};

  fill_band_infos(iwl_trans_get_mvm(sim_trans_.iwl_trans())->nvm_data, bands, ARRAY_SIZE(bands),
                  band_infos);
  // 2.4Ghz
  wlan_info_band_info_t* exp_band_info = &band_infos[0];
  EXPECT_EQ(WLAN_INFO_BAND_2GHZ, exp_band_info->band);
  EXPECT_EQ(true, exp_band_info->ht_supported);
  EXPECT_EQ(expected_rate(0), exp_band_info->rates[0]);    // 1Mbps
  EXPECT_EQ(expected_rate(11), exp_band_info->rates[11]);  // 54Mbps
  EXPECT_EQ(2407, exp_band_info->supported_channels.base_freq);
  EXPECT_EQ(1, exp_band_info->supported_channels.channels[0]);
  EXPECT_EQ(13, exp_band_info->supported_channels.channels[12]);
  // 5GHz
  exp_band_info = &band_infos[1];
  EXPECT_EQ(WLAN_INFO_BAND_5GHZ, exp_band_info->band);
  EXPECT_EQ(true, exp_band_info->ht_supported);
  EXPECT_EQ(expected_rate(4), exp_band_info->rates[0]);   // 6Mbps
  EXPECT_EQ(expected_rate(11), exp_band_info->rates[7]);  // 54Mbps
  EXPECT_EQ(5000, exp_band_info->supported_channels.base_freq);
  EXPECT_EQ(36, exp_band_info->supported_channels.channels[0]);
  EXPECT_EQ(165, exp_band_info->supported_channels.channels[24]);
}

TEST_F(WlanDeviceTest, FillBandInfosOnly5GHz) {
  // The default 'nvm_data' is loaded from test/sim-default-nvm.cc.

  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT] = {
      WLAN_INFO_BAND_5GHZ,
      0,
  };
  wlan_info_band_info_t band_infos[WLAN_INFO_BAND_COUNT] = {};

  fill_band_infos(iwl_trans_get_mvm(sim_trans_.iwl_trans())->nvm_data, bands, 1, band_infos);
  // 5GHz
  wlan_info_band_info_t* exp_band_info = &band_infos[0];
  EXPECT_EQ(WLAN_INFO_BAND_5GHZ, exp_band_info->band);
  EXPECT_EQ(true, exp_band_info->ht_supported);
  EXPECT_EQ(expected_rate(4), exp_band_info->rates[0]);   // 6Mbps
  EXPECT_EQ(expected_rate(11), exp_band_info->rates[7]);  // 54Mbps
  EXPECT_EQ(5000, exp_band_info->supported_channels.base_freq);
  EXPECT_EQ(36, exp_band_info->supported_channels.channels[0]);
  EXPECT_EQ(165, exp_band_info->supported_channels.channels[24]);
  // index 1 should be empty.
  exp_band_info = &band_infos[1];
  EXPECT_EQ(false, exp_band_info->ht_supported);
  EXPECT_EQ(0x00, exp_band_info->rates[0]);
  EXPECT_EQ(0x00, exp_band_info->rates[7]);
  EXPECT_EQ(0, exp_band_info->supported_channels.channels[0]);
}

/////////////////////////////////////       MAC       //////////////////////////////////////////////

TEST_F(WlanDeviceTest, MacQuery) {
  // Test input null pointers
  uint32_t options = 0;
  void* whatever = &options;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, wlanmac_ops.query(nullptr, options, nullptr));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, wlanmac_ops.query(whatever, options, nullptr));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            wlanmac_ops.query(nullptr, options, reinterpret_cast<wlanmac_info*>(whatever)));

  wlanmac_info_t info = {};
  ASSERT_EQ(ZX_OK, wlanmac_ops.query(&mvmvif_sta_, options, &info));
  EXPECT_EQ(WLAN_INFO_MAC_ROLE_CLIENT, info.ifc_info.mac_role);
  //
  // The below code assumes the test/sim-default-nvm.cc contains 2 bands.
  //
  //   .bands[0]: WLAN_INFO_BAND_2GHZ
  //   .bands[1]: WLAN_INFO_BAND_5GHZ
  //
  ASSERT_EQ(2, info.ifc_info.bands_count);
  EXPECT_EQ(expected_rate(0), info.ifc_info.bands[0].rates[0]);    // 1 Mbps
  EXPECT_EQ(expected_rate(7), info.ifc_info.bands[0].rates[7]);    // 18 Mbps
  EXPECT_EQ(expected_rate(11), info.ifc_info.bands[0].rates[11]);  // 54 Mbps
  EXPECT_EQ(expected_rate(4), info.ifc_info.bands[1].rates[0]);    // 6 Mbps
  EXPECT_EQ(165, info.ifc_info.bands[1].supported_channels.channels[24]);
}

TEST_F(WlanDeviceTest, MacStart) {
  // Test input null pointers
  wlanmac_ifc_protocol_ops_t proto_ops = {
      .recv = recv_wrapper,
  };
  wlanmac_ifc_protocol_t ifc = {.ops = &proto_ops};
  zx_handle_t sme_channel;
  ASSERT_EQ(wlanmac_ops.start(nullptr, &ifc, &sme_channel), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, nullptr, &sme_channel), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, nullptr), ZX_ERR_INVALID_ARGS);

  // Test callback function
  recv_cb_t mock_recv;  // To mock up the wlanmac_ifc_t.recv().
  mvmvif_sta_.sme_channel = sme_channel_;
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &sme_channel), ZX_OK);
  // Expect the above line would copy the 'ifc'. Then set expectation below and fire test.
  mock_recv.ExpectCall(&mock_recv, 0, nullptr, 0, nullptr);
  mvmvif_sta_.ifc.ops->recv(&mock_recv, 0, nullptr, 0, nullptr);
  mock_recv.VerifyAndClear();
}

TEST_F(WlanDeviceTest, MacStartSmeChannel) {
  // The normal case. A channel will be transferred to MLME.
  constexpr zx_handle_t from_devmgr = sme_channel_;
  mvmvif_sta_.sme_channel = from_devmgr;
  wlanmac_ifc_protocol_ops_t proto_ops = {
      .recv = recv_wrapper,
  };
  wlanmac_ifc_protocol_t ifc = {.ops = &proto_ops};
  zx_handle_t sme_channel;
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &sme_channel), ZX_OK);
  ASSERT_EQ(sme_channel, from_devmgr);                    // The channel handle is returned.
  ASSERT_EQ(mvmvif_sta_.sme_channel, ZX_HANDLE_INVALID);  // Driver no longer holds the ownership.

  // Since the driver no longer owns the handle, the start should fail.
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &sme_channel), ZX_ERR_ALREADY_BOUND);
}

TEST_F(WlanDeviceTest, MacUnbind) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .sme_channel = sme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();

  // Create an interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[iface_id];

  // Then unbind it
  device_mac_ops.unbind(mvmvif);
  // unbind() doesn't have return value. Expect it is not crashed.

  // Do again and expect not crashed
  device_mac_ops.unbind(mvmvif);
}

TEST_F(WlanDeviceTest, MacUnbindInvalidZxdev) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .sme_channel = sme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();

  // Create an interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[iface_id];

  // Invalidate the zxdev with whatever value
  mvmvif->zxdev = fake_ddk::kFakeParent;

  // Expect the unbind still cleans up the internal state.
  device_mac_ops.unbind(mvmvif);
  ASSERT_EQ(mvmvif->zxdev, nullptr);
}

TEST_F(WlanDeviceTest, MacRelease) {
  // Allocate an instance so that we can free that in mac_release().
  struct iwl_mvm_vif* mvmvif =
      reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));

  // Create a channel. Let this test case holds one end while driver holds the other end.
  char dummy[1];
  zx_handle_t case_end;
  ASSERT_EQ(zx_channel_create(0 /* option */, &case_end, &mvmvif->sme_channel), ZX_OK);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0), ZX_OK);

  // Call release and the sme channel should be closed so that we will get a peer-close error while
  // trying to write any data to it.
  device_mac_ops.release(mvmvif);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0),
            ZX_ERR_PEER_CLOSED);
}

/////////////////////////////////////       PHY       //////////////////////////////////////////////

TEST_F(WlanDeviceTest, PhyQuery) {
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();
  wlanphy_impl_info_t info = {};

  // Test input null pointers
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, wlanphy_ops.query(nullptr, nullptr));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, wlanphy_ops.query(iwl_trans, nullptr));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, wlanphy_ops.query(nullptr, &info));

  // Normal case
  ASSERT_EQ(ZX_OK, wlanphy_ops.query(iwl_trans, &info));
  EXPECT_EQ(WLAN_INFO_MAC_ROLE_CLIENT, info.wlan_info.mac_role);
  //
  // The below code assumes the test/sim-default-nvm.cc contains 2 bands.
  //
  //   .bands[0]: WLAN_INFO_BAND_2GHZ
  //   .bands[1]: WLAN_INFO_BAND_5GHZ
  //
  ASSERT_EQ(2, info.wlan_info.bands_count);
  EXPECT_EQ(expected_rate(0), info.wlan_info.bands[0].rates[0]);    // 1 Mbps
  EXPECT_EQ(expected_rate(7), info.wlan_info.bands[0].rates[7]);    // 18 Mbps
  EXPECT_EQ(expected_rate(11), info.wlan_info.bands[0].rates[11]);  // 54 Mbps
  EXPECT_EQ(expected_rate(4), info.wlan_info.bands[1].rates[0]);    // 6 Mbps
  EXPECT_EQ(165, info.wlan_info.bands[1].supported_channels.channels[24]);
}

TEST_F(WlanDeviceTest, PhyCreateDestroySingleInterface) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .sme_channel = sme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();

  // Test input null pointers
  ASSERT_EQ(wlanphy_ops.create_iface(nullptr, &req, &iface_id), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, nullptr, &iface_id), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanphy_ops.destroy_iface(nullptr, 0), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, MAX_NUM_MVMVIF), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_ERR_NOT_FOUND);  // hasn't been added yet.

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);

  // Add interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 0);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Remove interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[iface_id], nullptr);
}

TEST_F(WlanDeviceTest, PhyCreateDestroyMultipleInterfaces) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .sme_channel = sme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);  // To verify the internal state of MVM driver

  // Add 1st interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 0);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Add 2nd interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Add 3rd interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 2);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Remove the 2nd interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 1), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[1], nullptr);

  // Add a new interface and it should be the 2nd one.
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Add 4th interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 3);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Add 5th interface and it should fail
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_ERR_NO_RESOURCES);

  // Remove the 2nd interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 1), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[1], nullptr);

  // Remove the 3rd interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 2), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[2], nullptr);

  // Remove the 4th interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 3), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[3], nullptr);

  // Remove the 1st interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[0], nullptr);

  // Remove the 1st interface again and it should fail.
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_ERR_NOT_FOUND);
}

TEST_F(WlanDeviceTest, PhyDestroyInvalidZxdev) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .sme_channel = sme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);

  // Add interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);

  // Replace the zxdev with invalid value
  mvm->mvmvif[iface_id]->zxdev = fake_ddk::kFakeParent;

  // Remove interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[iface_id], nullptr);
}

}  // namespace
}  // namespace wlan::testing
