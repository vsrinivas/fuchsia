// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY and MAC device callback functions.

#include <fuchsia/wlan/common/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>

#include <list>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlanphy-impl-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/mock-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/wlan-pkt-builder.h"

namespace wlan::testing {
namespace {

static constexpr size_t kListenInterval = 100;

typedef mock_function::MockFunction<void, void*, uint32_t, const void*, size_t,
                                    const wlan_rx_info_t*>
    recv_cb_t;

// The wrapper used by wlanmac_ifc_t.recv() to call mock-up.
void recv_wrapper(void* cookie, uint32_t flags, const uint8_t* data, size_t length,
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
                    .beacon_int = kListenInterval,
                },
        } {
    device_ = sim_trans_.sim_device();
  }
  ~WlanDeviceTest() {}

 protected:
  static constexpr zx_handle_t mlme_channel_ =
      73939133;  // An arbitrary value not ZX_HANDLE_INVALID
  static constexpr uint8_t kInvalidBandIdFillByte = 0xa5;
  static constexpr wlan_info_band_t kInvalidBandId = 0xa5a5a5a5;
  struct iwl_mvm_vif mvmvif_sta_;  // The mvm_vif settings for station role.
  wlan::iwlwifi::WlanphyImplDevice* device_;
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
  EXPECT_EQ(WLAN_INFO_MAC_ROLE_CLIENT, info.mac_role);

  //
  // The below code assumes the test/sim-default-nvm.cc contains 2 bands.
  //
  //   .bands[0]: WLAN_INFO_BAND_2GHZ
  //   .bands[1]: WLAN_INFO_BAND_5GHZ
  //
  ASSERT_EQ(2, info.bands_count);
  EXPECT_EQ(expected_rate(0), info.bands[0].rates[0]);    // 1 Mbps
  EXPECT_EQ(expected_rate(7), info.bands[0].rates[7]);    // 18 Mbps
  EXPECT_EQ(expected_rate(11), info.bands[0].rates[11]);  // 54 Mbps
  EXPECT_EQ(expected_rate(4), info.bands[1].rates[0]);    // 6 Mbps
  EXPECT_EQ(165, info.bands[1].supported_channels.channels[24]);
}

TEST_F(WlanDeviceTest, MacStart) {
  // Test input null pointers
  wlanmac_ifc_protocol_ops_t proto_ops = {
      .recv = recv_wrapper,
  };
  wlanmac_ifc_protocol_t ifc = {.ops = &proto_ops};
  zx_handle_t mlme_channel;
  ASSERT_EQ(wlanmac_ops.start(nullptr, &ifc, &mlme_channel), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, nullptr, &mlme_channel), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, nullptr), ZX_ERR_INVALID_ARGS);

  // Test callback function
  recv_cb_t mock_recv;  // To mock up the wlanmac_ifc_t.recv().
  mvmvif_sta_.mlme_channel = mlme_channel_;
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &mlme_channel), ZX_OK);
  // Expect the above line would copy the 'ifc'. Then set expectation below and fire test.
  mock_recv.ExpectCall(&mock_recv, 0, nullptr, 0, nullptr);
  mvmvif_sta_.ifc.ops->recv(&mock_recv, 0, nullptr, 0, nullptr);
  mock_recv.VerifyAndClear();
}

TEST_F(WlanDeviceTest, MacStartSmeChannel) {
  // The normal case. A channel will be transferred to MLME.
  constexpr zx_handle_t from_devmgr = mlme_channel_;
  mvmvif_sta_.mlme_channel = from_devmgr;
  wlanmac_ifc_protocol_ops_t proto_ops = {
      .recv = recv_wrapper,
  };
  wlanmac_ifc_protocol_t ifc = {.ops = &proto_ops};
  zx_handle_t mlme_channel;
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &mlme_channel), ZX_OK);
  ASSERT_EQ(mlme_channel, from_devmgr);                    // The channel handle is returned.
  ASSERT_EQ(mvmvif_sta_.mlme_channel, ZX_HANDLE_INVALID);  // Driver no longer holds the ownership.

  // Since the driver no longer owns the handle, the start should fail.
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &mlme_channel), ZX_ERR_ALREADY_BOUND);
}

TEST_F(WlanDeviceTest, MacRelease) {
  // Allocate an instance so that we can free that in mac_release().
  struct iwl_mvm_vif* mvmvif =
      reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));

  // Create a channel. Let this test case holds one end while driver holds the other end.
  char dummy[1];
  zx_handle_t case_end;
  ASSERT_EQ(zx_channel_create(0 /* option */, &case_end, &mvmvif->mlme_channel), ZX_OK);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0), ZX_OK);

  // Call release and the sme channel should be closed so that we will get a peer-close error while
  // trying to write any data to it.
  device_mac_ops.release(mvmvif);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0),
            ZX_ERR_PEER_CLOSED);
}

/////////////////////////////////////       PHY       //////////////////////////////////////////////

TEST_F(WlanDeviceTest, PhyQuery) {
  wlanphy_impl_info_t info = {};

  // Test input null pointers
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanphyImplQuery(nullptr));
  ASSERT_EQ(ZX_OK, device_->WlanphyImplQuery(&info));

  // Normal case
  ASSERT_EQ(ZX_OK, device_->WlanphyImplQuery(&info));
  EXPECT_EQ(WLAN_INFO_MAC_ROLE_CLIENT, info.supported_mac_roles);
}

TEST_F(WlanDeviceTest, PhyPartialCreateCleanup) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .mlme_channel = mlme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();

  // Test input null pointers
  ASSERT_OK(phy_create_iface(iwl_trans, &req, &iface_id));

  // Ensure mvmvif got created and indexed.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  ASSERT_NOT_NULL(mvm->mvmvif[iface_id]);

  // Ensure partial create failure removes it from the index.
  phy_create_iface_undo(iwl_trans, iface_id);
  ASSERT_NULL(mvm->mvmvif[iface_id]);
}

TEST_F(WlanDeviceTest, PhyCreateDestroySingleInterface) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .mlme_channel = mlme_channel_,
  };
  uint16_t iface_id;

  // Test input null pointers
  ASSERT_EQ(device_->WlanphyImplCreateIface(nullptr, &iface_id), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(device_->WlanphyImplCreateIface(nullptr, nullptr), ZX_ERR_INVALID_ARGS);

  // Test invalid inputs
  ASSERT_EQ(device_->WlanphyImplDestroyIface(MAX_NUM_MVMVIF), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_ERR_NOT_FOUND);  // hasn't been added yet.

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());

  // Add interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 0);  // the first interface should have id 0.
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[iface_id];
  ASSERT_NE(mvmvif, nullptr);
  ASSERT_EQ(mvmvif->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  // Count includes phy device in addition to the newly created mac device.
  ASSERT_EQ(fake_parent_->descendant_count(), 2);
  device_->zxdev()->GetLatestChild()->InitOp();

  // Remove interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);
}

TEST_F(WlanDeviceTest, PhyCreateDestroyMultipleInterfaces) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_INFO_MAC_ROLE_CLIENT,
      .mlme_channel = mlme_channel_,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);  // To verify the internal state of MVM driver

  // Add 1st interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 0);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 2);
  device_->zxdev()->GetLatestChild()->InitOp();

  // Add 2nd interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);
  device_->zxdev()->GetLatestChild()->InitOp();

  // Add 3rd interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 2);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);
  device_->zxdev()->GetLatestChild()->InitOp();

  // Remove the 2nd interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(1), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);

  // Add a new interface and it should be the 2nd one.
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);
  device_->zxdev()->GetLatestChild()->InitOp();

  // Add 4th interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 3);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 5);
  device_->zxdev()->GetLatestChild()->InitOp();

  // Add 5th interface and it should fail
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_ERR_NO_RESOURCES);
  ASSERT_EQ(fake_parent_->descendant_count(), 5);

  // Remove the 2nd interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(1), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);

  // Remove the 3rd interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(2), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[2], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);

  // Remove the 4th interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(3), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[3], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 2);

  // Remove the 1st interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[0], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);

  // Remove the 1st interface again and it should fail.
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);
}

// The class for WLAN device MAC testing.
//
class MacInterfaceTest : public WlanDeviceTest, public MockTrans {
 public:
  MacInterfaceTest() : ifc_{ .ops = &proto_ops_, } , proto_ops_{ .recv = recv_wrapper, } {
    mvmvif_sta_.mlme_channel = mlme_channel_;
    zx_handle_t mlme_channel;
    ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc_, &mlme_channel), ZX_OK);

    // Add the interface to MVM instance.
    mvmvif_sta_.mvm->mvmvif[0] = &mvmvif_sta_;
  }

  ~MacInterfaceTest() {
    VerifyExpectation();  // Ensure all expectations had been met.

    // Restore the original callback for other test cases not using the mock.
    if (original_send_cmd) {
      sim_trans_.iwl_trans()->ops->send_cmd = original_send_cmd;
    }

    // Stop the MAC to free resources we allocated.
    // This must be called after we verify the expected commands and restore the mock command
    // callback so that the stop command doesn't mess up the test case expectation.
    wlanmac_ops.stop(&mvmvif_sta_);
    VerifyStaHasBeenRemoved();
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
  zx_status_t SetChannel(const wlan_channel_t* channel) {
    uint32_t option = 0;
    return wlanmac_ops.set_channel(&mvmvif_sta_, option, channel);
  }

  zx_status_t ConfigureBss(const bss_config_t* config) {
    uint32_t option = 0;
    return wlanmac_ops.configure_bss(&mvmvif_sta_, option, config);
  }

  zx_status_t ConfigureAssoc(const wlan_assoc_ctx_t* config) {
    uint32_t option = 0;
    return wlanmac_ops.configure_assoc(&mvmvif_sta_, option, config);
  }

  zx_status_t ClearAssoc() {
    uint32_t option = 0;
    uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN];  // Not used since all info were
                                                             // saved in mvmvif_sta_ already.
    return wlanmac_ops.clear_assoc(&mvmvif_sta_, option, peer_addr);
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

    mock_tx_.VerifyAndClear();
  }

  void VerifyStaHasBeenRemoved() {
    auto mvm = mvmvif_sta_.mvm;

    for (size_t i = 0; i < ARRAY_SIZE(mvm->fw_id_to_mac_id); i++) {
      struct iwl_mvm_sta* mvm_sta = mvm->fw_id_to_mac_id[i];
      ASSERT_EQ(nullptr, mvm_sta);
    }
    ASSERT_EQ(0, mvm->vif_count);
  }

  // Mock function for Tx.
  mock_function::MockFunction<zx_status_t,  // return value
                              size_t,       // packet size
                              uint16_t,     // cmd + group_id
                              int           // txq_id
                              >
      mock_tx_;

  static zx_status_t tx_wrapper(struct iwl_trans* trans, struct ieee80211_mac_packet* pkt,
                                const struct iwl_device_cmd* dev_cmd, int txq_id) {
    auto test = GET_TEST(MacInterfaceTest, trans);
    return test->mock_tx_.Call(pkt->header_size + pkt->headroom_used_size + pkt->body_size,
                               WIDE_ID(dev_cmd->hdr.group_id, dev_cmd->hdr.cmd), txq_id);
  }

  wlanmac_ifc_protocol_t ifc_;
  wlanmac_ifc_protocol_ops_t proto_ops_;
  static constexpr bss_config_t kBssConfig = {
      .bssid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      .bss_type = BSS_TYPE_INFRASTRUCTURE,
      .remote = true,
  };
  static constexpr wlan_assoc_ctx_t kAssocCtx = {
      .listen_interval = kListenInterval,
  };
};

// Test the set_channel().
//
TEST_F(MacInterfaceTest, TestSetChannel) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
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
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
  }));

  mvmvif_sta_.mac_role = WLAN_INFO_MAC_ROLE_AP;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, SetChannel(&kChannel));
}

// Tests calling SetChannel()/ConfigureBss() again without ConfigureAssoc()/ClearAssoc()
TEST_F(MacInterfaceTest, DuplicateSetChannel) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id];
  struct iwl_mvm_phy_ctxt* phy_ctxt = mvmvif_sta_.phy_ctxt;
  ASSERT_NE(nullptr, phy_ctxt);
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  // Call SetChannel() again. This should return the same phy context but ConfigureBss()
  // should setup a new STA.
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  struct iwl_mvm_phy_ctxt* new_phy_ctxt = mvmvif_sta_.phy_ctxt;
  ASSERT_NE(nullptr, new_phy_ctxt);
  ASSERT_EQ(phy_ctxt, new_phy_ctxt);
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* new_mvm_sta = mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id];
  // Now Associate and disassociate - this should release and reset the phy ctxt.
  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, new_mvm_sta->sta_state);
  ASSERT_EQ(true, mvmvif_sta_.bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_sta_.bss_conf.listen_interval);

  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_sta_.phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_sta_.ap_sta_id);
}

// Test ConfigureBss()
//
TEST_F(MacInterfaceTest, TestConfigureBss) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));

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
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, ConfigureBss(&kBssConfig));
}

// Test unsupported bss_type.
//
TEST_F(MacInterfaceTest, UnsupportedBssType) {
  static constexpr bss_config_t kUnsupportedBssConfig = {
      .bssid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      .bss_type = BSS_TYPE_INDEPENDENT,
      .remote = true,
  };
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ConfigureBss(&kUnsupportedBssConfig));
}

// Test failed ADD_STA command.
//
TEST_F(MacInterfaceTest, TestFailedAddSta) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));

  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA), kSimMvmReturnWithStatus,
                  ZX_ERR_BUFFER_TOO_SMALL /* an arbitrary error */),
  }));

  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, ConfigureBss(&kBssConfig));
}

// Test exception handling in driver.
//
TEST_F(MacInterfaceTest, TestExceptionHandling) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));

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

// The test is used to test the typical procedure to connect to an open network.
//
TEST_F(MacInterfaceTest, AssociateToOpenNetwork) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_sta_.mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_EQ(true, mvmvif_sta_.bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_sta_.bss_conf.listen_interval);

  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_sta_.phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_sta_.ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
}

// Back to back calls of ClearAssoc().
TEST_F(MacInterfaceTest, ClearAssocAfterClearAssoc) {
  ASSERT_NE(ZX_OK, ClearAssoc());
  ASSERT_NE(ZX_OK, ClearAssoc());
}

// ClearAssoc() should cleanup when called without Assoc
TEST_F(MacInterfaceTest, ClearAssocAfterNoAssoc) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_sta_.mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_sta_.phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_sta_.ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
  // Call ClearAssoc() again to check if it is handled correctly.
  ASSERT_NE(ZX_OK, ClearAssoc());
}

TEST_F(MacInterfaceTest, AssociateToOpenNetworkNullStation) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);

  // Replace the STA pointer with NULL and expect the association will fail.
  auto org = mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id];
  mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id] = nullptr;

  ASSERT_EQ(ZX_ERR_BAD_STATE, ConfigureAssoc(&kAssocCtx));

  // Expect error while disassociating a non-existing association.
  ASSERT_EQ(ZX_ERR_BAD_STATE, ClearAssoc());

  // We have to recover the pointer so that the MAC stop function can recycle the memory.
  mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id] = org;
}

TEST_F(MacInterfaceTest, ClearAssocAfterFailedAssoc) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);

  struct iwl_mvm* mvm = mvmvif_sta_.mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);
  // Replace the STA pointer with NULL and expect the association will fail.
  auto org = mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id];
  mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id] = nullptr;

  ASSERT_EQ(ZX_ERR_BAD_STATE, ConfigureAssoc(&kAssocCtx));
  // Now put back the original STA pointer so ClearAssoc runs and also
  // to recycle allocated memory
  mvmvif_sta_.mvm->fw_id_to_mac_id[mvmvif_sta_.ap_sta_id] = org;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  // Expect error while disassociating a non-existing association.
  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_sta_.phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_sta_.ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
  // Call ClearAssoc() again to check if it is handled correctly.
  ASSERT_NE(ZX_OK, ClearAssoc());
}

TEST_F(MacInterfaceTest, TxPktNotSupportedRole) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  // Set to an unsupported role.
  mvmvif_sta_.mac_role = WLAN_INFO_MAC_ROLE_AP;

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build();
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, wlanmac_ops.queue_tx(&mvmvif_sta_, 0, wlan_pkt->wlan_pkt()));
  unbindTx();
}

// To test if a packet can be sent out.
TEST_F(MacInterfaceTest, TxPkt) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build();
  mock_tx_.ExpectCall(ZX_OK, wlan_pkt->len(), WIDE_ID(0, TX_CMD), IWL_MVM_DQA_MIN_MGMT_QUEUE);
  ASSERT_EQ(ZX_OK, wlanmac_ops.queue_tx(&mvmvif_sta_, 0, wlan_pkt->wlan_pkt()));
  unbindTx();
}

}  // namespace
}  // namespace wlan::testing
