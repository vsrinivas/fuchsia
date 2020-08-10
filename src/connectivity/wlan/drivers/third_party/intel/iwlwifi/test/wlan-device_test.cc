// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY and MAC device callback functions.

#include <zircon/syscalls.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"
}

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-function/mock-function.h>
#include <stdio.h>
#include <zircon/syscalls.h>

#include <list>

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
            .bss_conf =
                {
                    .beacon_int = 100,
                },
        } {
    struct iwl_mvm* mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());
    mtx_lock(&mvm->mutex);
    zx_status_t ret = iwl_nvm_init(mvm);
    mtx_unlock(&mvm->mutex);
    EXPECT_EQ(ZX_OK, ret);
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

  device_mac_ops.release(mvmvif);
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

  device_mac_ops.release(mvmvif);
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
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[iface_id];
  ASSERT_NE(mvmvif, nullptr);
  ASSERT_EQ(mvmvif->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);

  // Remove interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[iface_id], nullptr);

  device_mac_ops.release(mvmvif);
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
  struct iwl_mvm_vif* mvmvif0 = mvm->mvmvif[iface_id];

  // Add 2nd interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  struct iwl_mvm_vif* mvmvif1 = mvm->mvmvif[iface_id];

  // Add 3rd interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 2);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  struct iwl_mvm_vif* mvmvif2 = mvm->mvmvif[iface_id];

  // Remove the 2nd interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 1), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  device_mac_ops.release(mvmvif1);

  // Add a new interface and it should be the 2nd one.
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  mvmvif1 = mvm->mvmvif[iface_id];

  // Add 4th interface
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 3);  // the first interface should have id 0.
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  struct iwl_mvm_vif* mvmvif3 = mvm->mvmvif[iface_id];

  // Add 5th interface and it should fail
  ASSERT_EQ(wlanphy_ops.create_iface(iwl_trans, &req, &iface_id), ZX_ERR_NO_RESOURCES);

  // Remove the 2nd interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 1), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  device_mac_ops.release(mvmvif1);

  // Remove the 3rd interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 2), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[2], nullptr);
  device_mac_ops.release(mvmvif2);

  // Remove the 4th interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 3), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[3], nullptr);
  device_mac_ops.release(mvmvif3);

  // Remove the 1st interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[0], nullptr);
  device_mac_ops.release(mvmvif0);

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
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[iface_id];

  // Replace the zxdev with invalid value
  mvm->mvmvif[iface_id]->zxdev = fake_ddk::kFakeParent;

  // Remove interface
  ASSERT_EQ(wlanphy_ops.destroy_iface(iwl_trans, 0), ZX_OK);
  ASSERT_EQ(mvm->mvmvif[iface_id], nullptr);
  device_mac_ops.release(mvmvif);
}

// The class for WLAN device MAC testing.
//
class MacInterfaceTest : public WlanDeviceTest {
 public:
  MacInterfaceTest() : ifc_{ .ops = &proto_ops_, } , proto_ops_{ .recv = recv_wrapper, } {
    mvmvif_sta_.sme_channel = sme_channel_;
    zx_handle_t sme_channel;
    ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc_, &sme_channel), ZX_OK);

    // Add the interface to MVM instance.
    mvmvif_sta_.mvm->mvmvif[0] = &mvmvif_sta_;
    mvmvif_sta_.mvm->vif_count++;
  }

  ~MacInterfaceTest() {
    VerifyExpectation();  // Ensure all expectations had been met.

    // Restore the original callback for other test cases not using the mock.
    if (original_send_cmd) {
      sim_trans_.iwl_trans()->ops->send_cmd = original_send_cmd;
    }
  }

  // Used in MockCommand constructor to indicate if the command needs to be either
  //
  //   - returned immediately (with a status code), or
  //   - passed to the sim_mvm.c.
  //
  enum SimMvmBehavior {
    kSimMvmReturnWithStatus,
    kSimMvmBypassToSimMvm,
  };

  // A flexible mock-up of firmware command for testing code. Testing code can decide to either call
  // the simulated firmware or return the status code immediately.
  //
  //   cmd_id: the command ID. Sometimes composed with WIDE_ID() macro.
  //   behavior: determine what this mockup command is to do.
  //   status: the status code to return when behavior is 'kSimMvmReturnWithStatus'.
  //
  class MockCommand {
   public:
    MockCommand(uint32_t cmd_id, SimMvmBehavior behavior, zx_status_t status)
        : cmd_id_(cmd_id), behavior_(behavior), status_(status) {}
    MockCommand(uint32_t cmd_id) : MockCommand(cmd_id, kSimMvmBypassToSimMvm, ZX_OK) {}

    ~MockCommand() {}

    uint32_t cmd_id_;
    SimMvmBehavior behavior_;
    zx_status_t status_;
  };
  typedef std::list<MockCommand> expected_cmd_id_list;
  typedef zx_status_t (*fp_send_cmd)(struct iwl_trans* trans, struct iwl_host_cmd* cmd);

  // Public for MockSendCmd().
  expected_cmd_id_list expected_cmd_ids;
  fp_send_cmd original_send_cmd;

 protected:
  zx_status_t SetChannel(const wlan_channel_t* chan) {
    uint32_t option = 0;
    return wlanmac_ops.set_channel(&mvmvif_sta_, option, chan);
  }

  zx_status_t ConfigureBss(const wlan_bss_config_t* config) {
    uint32_t option = 0;
    return wlanmac_ops.configure_bss(&mvmvif_sta_, option, config);
  }

  // The following functions are for mocking up the firmware commands.
  //
  // The mock function will return the special error ZX_ERR_INTERNAL when the expectation
  // is not expected.

  // Set the expected commands sending to the firmware.
  //
  // Args:
  //   cmd_ids: list of expected commands. Will be matched in order.
  //
  void ExpectSendCmd(const expected_cmd_id_list& cmd_ids) {
    expected_cmd_ids = cmd_ids;

    // Re-define the 'dev' field in the 'struct iwl_trans' to a test instance of this class.
    sim_trans_.iwl_trans()->dev = reinterpret_cast<struct device*>(this);

    // Setup the mock function for send command.
    original_send_cmd = sim_trans_.iwl_trans()->ops->send_cmd;
    sim_trans_.iwl_trans()->ops->send_cmd = MockSendCmd;
  }

  static zx_status_t MockSendCmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
    MacInterfaceTest* this_ = reinterpret_cast<MacInterfaceTest*>(trans->dev);

    // remove the first one and match.
    expected_cmd_id_list& expected = this_->expected_cmd_ids;
    ZX_ASSERT_MSG(!expected.empty(),
                  "A command (0x%04x) is going to send, but no command is expected.\n", cmd->id);

    // check the command ID.
    auto exp = expected.front();
    ZX_ASSERT_MSG(exp.cmd_id_ == cmd->id,
                  "The command doesn't match! Expect: 0x%04x, actual: 0x%04x.\n", exp.cmd_id_,
                  cmd->id);
    expected.pop_front();

    if (exp.behavior_ == kSimMvmBypassToSimMvm) {
      return this_->original_send_cmd(trans, cmd);
    } else {
      return exp.status_;
    }
  }

  void VerifyExpectation() {
    for (expected_cmd_id_list::iterator it = expected_cmd_ids.begin(); it != expected_cmd_ids.end();
         it++) {
      printf("  ==> 0x%04x\n", it->cmd_id_);
    }
    ASSERT_TRUE(expected_cmd_ids.empty(), "The expected command set is not empty.");
  }

  wlanmac_ifc_protocol_t ifc_;
  wlanmac_ifc_protocol_ops_t proto_ops_;
  static constexpr wlan_bss_config_t kBssConfig = {
      .bssid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .remote = true,
  };
};

// Test the set_channel().
//
TEST_F(MacInterfaceTest, TestSetChannel) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),
  }));

  mvmvif_sta_.csa_bcn_pending = true;  // Expect to be clear because this is client role.
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  EXPECT_EQ(false, mvmvif_sta_.csa_bcn_pending);
}

// Test the unsupported MAC role.
//
TEST_F(MacInterfaceTest, TestSetChannelWithUnsupportedRole) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),
  }));

  mvmvif_sta_.mac_role = WLAN_INFO_MAC_ROLE_AP;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, SetChannel(&kChannel));
}

// Test ConfigureBss()
//
TEST_F(MacInterfaceTest, TestConfigureBss) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, TIME_EVENT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, SCD_QUEUE_CFG)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
  }));

  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
}

// Test duplicate BSS config.
//
TEST_F(MacInterfaceTest, DuplicateConfigureBss) {
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, ConfigureBss(&kBssConfig));
}

// Test unsupported bss_type.
//
TEST_F(MacInterfaceTest, UnsupportedBssType) {
  static constexpr wlan_bss_config_t kUnsupportedBssConfig = {
      .bssid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      .bss_type = WLAN_BSS_TYPE_IBSS,
      .remote = true,
  };
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ConfigureBss(&kUnsupportedBssConfig));
}

// Test failed ADD_STA command.
//
TEST_F(MacInterfaceTest, TestFailedAddSta) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA), kSimMvmReturnWithStatus,
                  ZX_ERR_BUFFER_TOO_SMALL /* an arbitrary error */),
  }));

  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, ConfigureBss(&kBssConfig));
}

// Test exception handling in driver.
//
TEST_F(MacInterfaceTest, TestExceptionHandling) {
  // Test the beacon interval checking.
  mvmvif_sta_.bss_conf.beacon_int = 0;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ConfigureBss(&kBssConfig));
  mvmvif_sta_.bss_conf.beacon_int = 16;  // which just passes the check.

  // Test the phy_ctxt checking.
  auto backup_phy_ctxt = mvmvif_sta_.phy_ctxt;
  mvmvif_sta_.phy_ctxt = nullptr;
  EXPECT_EQ(ZX_ERR_BAD_STATE, ConfigureBss(&kBssConfig));
  mvmvif_sta_.phy_ctxt = backup_phy_ctxt;

  // Test the case we run out of slots for STA.
  //
  // In the constructor of the test, mvmvif_sta_ had been added once. So we would expect the
  // following (IWL_MVM_STATION_COUNT - 1) adding would be successful as well.
  //
  for (size_t i = 0; i < IWL_MVM_STATION_COUNT - 1; i++) {
    // Pretent the STA is not assigned so that we can add it again.
    mvmvif_sta_.ap_sta_id = IWL_MVM_INVALID_STA;
    ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  }
  // However, the last one should fail because we run out of all slots in fw_id_to_mac_id[].
  mvmvif_sta_.ap_sta_id = IWL_MVM_INVALID_STA;
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
}

// The test is used to test the typical procedure to connect to an open network. Will be updated
// in the following CLs.
//
TEST_F(MacInterfaceTest, AssociateToOpenNetwork) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
}

}  // namespace
}  // namespace wlan::testing
