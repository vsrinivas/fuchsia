// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlan-softmac-device.h"

#include <fidl/fuchsia.wlan.ieee80211/cpp/wire_types.h>
#include <fuchsia/hardware/wlan/associnfo/cpp/banjo.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/ieee80211/c/fidl.h>
#include <lib/mock-function/mock-function.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <iterator>
#include <list>
#include <memory>
#include <utility>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-nvm-parse.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/scoped_utils.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/mock-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/wlan-pkt-builder.h"

namespace wlan::testing {
namespace {

constexpr size_t kListenInterval = 100;
constexpr uint8_t kInvalidBandIdFillByte = 0xa5;
constexpr wlan_info_band_t kInvalidBandId = 0xa5a5a5a5;
constexpr zx_handle_t kDummyMlmeChannel = 73939133;  // An arbitrary value not ZX_HANDLE_INVALID

using recv_cb_t = mock_function::MockFunction<void, void*, const wlan_rx_packet_t*>;

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

// The wrapper used by wlan_softmac_ifc_t.recv() to call mock-up.
void recv_wrapper(void* cookie, const wlan_rx_packet_t* packet) {
  auto recv = reinterpret_cast<recv_cb_t*>(cookie);
  recv->Call(cookie, packet);
}

class WlanSoftmacDeviceTest : public SingleApTest {
 public:
  WlanSoftmacDeviceTest() {
    mvmvif_.reset(reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif))));
    mvmvif_->mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());
    mvmvif_->mlme_channel = kDummyMlmeChannel;
    mvmvif_->mac_role = WLAN_MAC_ROLE_CLIENT;
    mvmvif_->bss_conf = {.beacon_int = kListenInterval};

    device_ = std::make_unique<::wlan::iwlwifi::WlanSoftmacDevice>(nullptr, sim_trans_.iwl_trans(),
                                                                   0, mvmvif_.get());
  }
  ~WlanSoftmacDeviceTest() override {}

 protected:
  wlan::iwlwifi::unique_free_ptr<struct iwl_mvm_vif> mvmvif_;
  std::unique_ptr<::wlan::iwlwifi::WlanSoftmacDevice> device_;
};

//////////////////////////////////// Helper Functions  /////////////////////////////////////////////

TEST_F(WlanSoftmacDeviceTest, ComposeBandList) {
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
  EXPECT_EQ(WLAN_INFO_BAND_TWO_GHZ, bands[0]);
  EXPECT_EQ(kInvalidBandId, bands[1]);

  // 5GHz only
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_52ghz_enable = true;
  EXPECT_EQ(1, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(WLAN_INFO_BAND_FIVE_GHZ, bands[0]);
  EXPECT_EQ(kInvalidBandId, bands[1]);

  // both bands enabled
  memset(&nvm_data, 0, sizeof(nvm_data));
  memset(bands, kInvalidBandIdFillByte, sizeof(bands));
  nvm_data.sku_cap_band_24ghz_enable = true;
  nvm_data.sku_cap_band_52ghz_enable = true;
  EXPECT_EQ(2, compose_band_list(&nvm_data, bands));
  EXPECT_EQ(WLAN_INFO_BAND_TWO_GHZ, bands[0]);
  EXPECT_EQ(WLAN_INFO_BAND_FIVE_GHZ, bands[1]);
}

TEST_F(WlanSoftmacDeviceTest, FillBandCapabilityList) {
  // The default 'nvm_data' is loaded from test/sim-default-nvm.cc.

  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT] = {
      WLAN_INFO_BAND_TWO_GHZ,
      WLAN_INFO_BAND_FIVE_GHZ,
  };
  wlan_softmac_band_capability_t band_cap_list[WLAN_INFO_BAND_COUNT] = {};

  fill_band_cap_list(iwl_trans_get_mvm(sim_trans_.iwl_trans())->nvm_data, bands, std::size(bands),
                     band_cap_list);
  // 2.4Ghz
  wlan_softmac_band_capability_t* band_cap = &band_cap_list[0];
  EXPECT_EQ(WLAN_INFO_BAND_TWO_GHZ, band_cap->band);
  EXPECT_EQ(true, band_cap->ht_supported);
  EXPECT_EQ(expected_rate(0), band_cap->rates[0]);    // 1Mbps
  EXPECT_EQ(expected_rate(11), band_cap->rates[11]);  // 54Mbps
  EXPECT_EQ(2407, band_cap->supported_channels.base_freq);
  EXPECT_EQ(1, band_cap->supported_channels.channels[0]);
  EXPECT_EQ(13, band_cap->supported_channels.channels[12]);
  // 5GHz
  band_cap = &band_cap_list[1];
  EXPECT_EQ(WLAN_INFO_BAND_FIVE_GHZ, band_cap->band);
  EXPECT_EQ(true, band_cap->ht_supported);
  EXPECT_EQ(expected_rate(4), band_cap->rates[0]);   // 6Mbps
  EXPECT_EQ(expected_rate(11), band_cap->rates[7]);  // 54Mbps
  EXPECT_EQ(5000, band_cap->supported_channels.base_freq);
  EXPECT_EQ(36, band_cap->supported_channels.channels[0]);
  EXPECT_EQ(165, band_cap->supported_channels.channels[24]);
}

TEST_F(WlanSoftmacDeviceTest, FillBandCapabilityListOnly5GHz) {
  // The default 'nvm_data' is loaded from test/sim-default-nvm.cc.

  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT] = {
      WLAN_INFO_BAND_FIVE_GHZ,
      0,
  };
  wlan_softmac_band_capability_t band_cap_list[WLAN_INFO_BAND_COUNT] = {};

  fill_band_cap_list(iwl_trans_get_mvm(sim_trans_.iwl_trans())->nvm_data, bands, 1, band_cap_list);
  // 5GHz
  wlan_softmac_band_capability_t* band_cap = &band_cap_list[0];
  EXPECT_EQ(WLAN_INFO_BAND_FIVE_GHZ, band_cap->band);
  EXPECT_EQ(true, band_cap->ht_supported);
  EXPECT_EQ(expected_rate(4), band_cap->rates[0]);   // 6Mbps
  EXPECT_EQ(expected_rate(11), band_cap->rates[7]);  // 54Mbps
  EXPECT_EQ(5000, band_cap->supported_channels.base_freq);
  EXPECT_EQ(36, band_cap->supported_channels.channels[0]);
  EXPECT_EQ(165, band_cap->supported_channels.channels[24]);
  // index 1 should be empty.
  band_cap = &band_cap_list[1];
  EXPECT_EQ(false, band_cap->ht_supported);
  EXPECT_EQ(0x00, band_cap->rates[0]);
  EXPECT_EQ(0x00, band_cap->rates[7]);
  EXPECT_EQ(0, band_cap->supported_channels.channels[0]);
}

TEST_F(WlanSoftmacDeviceTest, Query) {
  // Test input null pointers
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanSoftmacQuery(nullptr));

  wlan_softmac_info_t info = {};
  ASSERT_EQ(ZX_OK, device_->WlanSoftmacQuery(&info));
  EXPECT_EQ(WLAN_MAC_ROLE_CLIENT, info.mac_role);

  //
  // The below code assumes the test/sim-default-nvm.cc contains 2 bands.
  //
  //   .band_cap_list[0]: WLAN_INFO_BAND_TWO_GHZ
  //   .band_cap_list[1]: WLAN_INFO_BAND_FIVE_GHZ
  //
  ASSERT_EQ(2, info.band_cap_count);
  EXPECT_EQ(expected_rate(0), info.band_cap_list[0].rates[0]);    // 1 Mbps
  EXPECT_EQ(expected_rate(7), info.band_cap_list[0].rates[7]);    // 18 Mbps
  EXPECT_EQ(expected_rate(11), info.band_cap_list[0].rates[11]);  // 54 Mbps
  EXPECT_EQ(expected_rate(4), info.band_cap_list[1].rates[0]);    // 6 Mbps
  EXPECT_EQ(165, info.band_cap_list[1].supported_channels.channels[24]);
}

TEST_F(WlanSoftmacDeviceTest, MacStart) {
  // Test input null pointers
  wlan_softmac_ifc_protocol_ops_t proto_ops = {
      .recv = recv_wrapper,
  };
  wlan_softmac_ifc_protocol_t ifc = {.ops = &proto_ops};
  zx::channel mlme_channel;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanSoftmacStart(nullptr, &mlme_channel));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanSoftmacStart(&ifc, nullptr));

  // Test callback function
  recv_cb_t mock_recv;  // To mock up the wlan_softmac_ifc_t.recv().
  mlme_channel = zx::channel(static_cast<zx_handle_t>(0xF000));
  ASSERT_EQ(ZX_OK, device_->WlanSoftmacStart(&ifc, &mlme_channel));
  // Expect the above line would copy the 'ifc'. Then set expectation below and fire test.
  mock_recv.ExpectCall(&mock_recv, nullptr);
  mvmvif_->ifc.ops->recv(&mock_recv, nullptr);
  mock_recv.VerifyAndClear();
}

TEST_F(WlanSoftmacDeviceTest, MacStartSmeChannel) {
  // The normal case. A channel will be transferred to MLME.
  constexpr zx_handle_t kChannelOne = static_cast<zx_handle_t>(0xF001);
  constexpr zx_handle_t kChannelTwo = static_cast<zx_handle_t>(0xF002);
  mvmvif_->mlme_channel = kChannelOne;
  wlan_softmac_ifc_protocol_ops_t proto_ops = {
      .recv = recv_wrapper,
  };
  wlan_softmac_ifc_protocol_t ifc = {.ops = &proto_ops};
  zx::channel mlme_channel(kChannelTwo);
  ASSERT_EQ(ZX_OK, device_->WlanSoftmacStart(&ifc, &mlme_channel));
  ASSERT_EQ(mlme_channel.get(), kChannelOne);           // The channel handle is returned.
  ASSERT_EQ(mvmvif_->mlme_channel, ZX_HANDLE_INVALID);  // Driver no longer holds the ownership.

  // Since the driver no longer owns the handle, the start should fail.
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, device_->WlanSoftmacStart(&ifc, &mlme_channel));
}

TEST_F(WlanSoftmacDeviceTest, Release) {
  // Create a channel. Let this test case holds one end while driver holds the other end.
  char dummy[1];
  zx_handle_t case_end;
  ASSERT_EQ(zx_channel_create(0 /* option */, &case_end, &mvmvif_->mlme_channel), ZX_OK);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0), ZX_OK);

  // Call release and the sme channel should be closed so that we will get a peer-close error while
  // trying to write any data to it.
  mvmvif_.release();
  device_.release()->DdkRelease();
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0),
            ZX_ERR_PEER_CLOSED);
}

// The class for WLAN device MAC testing.
//
class MacInterfaceTest : public WlanSoftmacDeviceTest, public MockTrans {
 public:
  MacInterfaceTest() : ifc_{ .ops = &proto_ops_, } , proto_ops_{ .recv = recv_wrapper, } {
    zx_handle_t wlanphy_impl_channel = mvmvif_->mlme_channel;
    zx::channel mlme_channel;
    ASSERT_EQ(ZX_OK, device_->WlanSoftmacStart(&ifc_, &mlme_channel));
    ASSERT_EQ(wlanphy_impl_channel, mlme_channel);

    // Add the interface to MVM instance.
    mvmvif_->mvm->mvmvif[0] = mvmvif_.get();
  }

  ~MacInterfaceTest() {
    VerifyExpectation();  // Ensure all expectations had been met.

    // Restore the original callback for other test cases not using the mock.
    ResetSendCmdFunc();

    // Stop the MAC to free resources we allocated.
    // This must be called after we verify the expected commands and restore the mock command
    // callback so that the stop command doesn't mess up the test case expectation.
    device_->WlanSoftmacStop();
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
    return device_->WlanSoftmacSetChannel(channel);
  }

  zx_status_t ConfigureBss(const bss_config_t* config) {
    return device_->WlanSoftmacConfigureBss(config);
  }

  zx_status_t ConfigureAssoc(const wlan_assoc_ctx_t* config) {
    return device_->WlanSoftmacConfigureAssoc(config);
  }

  zx_status_t ClearAssoc() {
    // Not used since all info were saved in mvmvif_sta_ already.
    uint8_t peer_addr[::fuchsia_wlan_ieee80211::wire::kMacAddrLen];
    return device_->WlanSoftmacClearAssoc(peer_addr);
  }

  zx_status_t SetKey(const wlan_key_config_t* key_config) {
    IWL_INFO(nullptr, "Calling set_key");
    return device_->WlanSoftmacSetKey(key_config);
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

  // Reset the send command function to the original one, so that the test case would stop checking
  // commands one by one.
  void ResetSendCmdFunc() {
    if (original_send_cmd) {
      IWL_INFO(nullptr, "Reseting send_cmd.");
      sim_trans_.iwl_trans()->ops->send_cmd = original_send_cmd;
    } else {
      IWL_WARN(nullptr, "No original send_cmd found.");
    }
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
    auto mvm = mvmvif_->mvm;

    for (size_t i = 0; i < std::size(mvm->fw_id_to_mac_id); i++) {
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

  wlan_softmac_ifc_protocol_t ifc_;
  wlan_softmac_ifc_protocol_ops_t proto_ops_;
  static constexpr bss_config_t kBssConfig = {
      .bssid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
      .bss_type = BSS_TYPE_INFRASTRUCTURE,
      .remote = true,
  };
  // Assoc context without HT related data.
  static constexpr wlan_assoc_ctx_t kAssocCtx = {
      .listen_interval = kListenInterval,
  };

  // Assoc context with HT related data. (The values below comes from real data in manual test)
  static constexpr wlan_assoc_ctx_t kHtAssocCtx = {
      .listen_interval = kListenInterval,
      .channel =
          {
              .primary = 157,
              .cbw = CHANNEL_BANDWIDTH_CBW80,
          },
      .rates_cnt = 8,
      .rates =
          {
              140,
              18,
              152,
              36,
              176,
              72,
              96,
              108,
          },
      .has_ht_cap = true,
      .ht_cap =
          {
              .supported_mcs_set =
                  {
                      .bytes =
                          {
                              255,
                              255,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              1,
                              0,
                              0,
                              0,
                          },
                  },
          },
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

  mvmvif_->csa_bcn_pending = true;  // Expect to be clear because this is client role.
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  EXPECT_EQ(false, mvmvif_->csa_bcn_pending);
}

// Test call set_channel() multiple times.
//
TEST_F(MacInterfaceTest, TestMultipleSetChannel) {
  ExpectSendCmd(expected_cmd_id_list({
      // for the first SetChannel()
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),

      // for the second SetChannel()
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),  // for remove_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, BINDING_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_PM_POWER_TABLE)),
  }));

  for (size_t i = 0; i < 2; i++) {
    ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  }
}

// Test the unsupported MAC role.
//
TEST_F(MacInterfaceTest, TestSetChannelWithUnsupportedRole) {
  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for add_chanctx
      MockCommand(WIDE_ID(LONG_GROUP, PHY_CONTEXT_CMD)),  // for change_chanctx
  }));

  mvmvif_->mac_role = WLAN_MAC_ROLE_AP;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, SetChannel(&kChannel));
}

// Tests calling SetChannel()/ConfigureBss() again without ConfigureAssoc()/ClearAssoc()
TEST_F(MacInterfaceTest, DuplicateSetChannel) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  struct iwl_mvm_phy_ctxt* phy_ctxt = mvmvif_->phy_ctxt;
  ASSERT_NE(nullptr, phy_ctxt);
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  // Call SetChannel() again. This should return the same phy context but ConfigureBss()
  // should setup a new STA.
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  struct iwl_mvm_phy_ctxt* new_phy_ctxt = mvmvif_->phy_ctxt;
  ASSERT_NE(nullptr, new_phy_ctxt);
  ASSERT_EQ(phy_ctxt, new_phy_ctxt);
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* new_mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  // Now Associate and disassociate - this should release and reset the phy ctxt.
  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, new_mvm_sta->sta_state);
  ASSERT_EQ(true, mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);

  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
}

// Test ConfigureBss()
//
TEST_F(MacInterfaceTest, TestConfigureBss) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));

  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, TIME_EVENT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
      MockCommand(WIDE_ID(LONG_GROUP, SCD_QUEUE_CFG)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
  }));

  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  // Ensure the BSSID was copied into mvmvif
  ASSERT_EQ(memcmp(mvmvif_->bss_conf.bssid, kBssConfig.bssid, ETH_ALEN), 0);
  ASSERT_EQ(memcmp(mvmvif_->bssid, kBssConfig.bssid, ETH_ALEN), 0);
}

// Test duplicate BSS config.
//
TEST_F(MacInterfaceTest, DuplicateConfigureBss) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  ASSERT_EQ(ZX_ERR_ALREADY_BOUND, ConfigureBss(&kBssConfig));
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
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD), kSimMvmReturnWithStatus,
                  ZX_ERR_BUFFER_TOO_SMALL /* an arbitrary error */),
  }));

  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, ConfigureBss(&kBssConfig));
}

// Test exception handling in driver.
//
TEST_F(MacInterfaceTest, TestExceptionHandling) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));

  // Test the beacon interval checking.
  mvmvif_->bss_conf.beacon_int = 0;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ConfigureBss(&kBssConfig));
  mvmvif_->bss_conf.beacon_int = 16;  // which just passes the check.

  // Test the phy_ctxt checking.
  auto backup_phy_ctxt = mvmvif_->phy_ctxt;
  mvmvif_->phy_ctxt = nullptr;
  EXPECT_EQ(ZX_ERR_BAD_STATE, ConfigureBss(&kBssConfig));
  mvmvif_->phy_ctxt = backup_phy_ctxt;

  // Test the case we run out of slots for STA.
  std::list<std::unique_ptr<::wlan::iwlwifi::WlanSoftmacDevice>> devices;
  for (size_t i = 0; i < IWL_MVM_STATION_COUNT; i++) {
    // Pretend the STA is not assigned so that we can add it again.
    devices.emplace_back(std::make_unique<::wlan::iwlwifi::WlanSoftmacDevice>(
        nullptr, sim_trans_.iwl_trans(), 0, mvmvif_.get()));
    std::swap(device_, devices.back());
    ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  }

  // However, the last one should fail because we run out of all slots in fw_id_to_mac_id[].
  devices.emplace_back(std::make_unique<::wlan::iwlwifi::WlanSoftmacDevice>(
      nullptr, sim_trans_.iwl_trans(), 0, mvmvif_.get()));
  std::swap(device_, devices.back());
  ASSERT_EQ(ZX_ERR_NO_RESOURCES, ConfigureBss(&kBssConfig));
}

// The test is used to test the typical procedure to connect to an open network.
//
TEST_F(MacInterfaceTest, AssociateToOpenNetwork) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_EQ(true, mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);
  ASSERT_EQ(mvm_sta->sta_state, iwl_sta_state::IWL_STA_AUTHORIZED);

  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
}

// Check if calling iwl_mvm_mac_sta_state() sets the state correctly.
TEST_F(MacInterfaceTest, CheckStaState) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_EQ(true, mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);
  ASSERT_EQ(mvm_sta->sta_state, iwl_sta_state::IWL_STA_AUTHORIZED);

  ASSERT_EQ(ZX_OK,
            iwl_mvm_mac_sta_state(mvmvif_.get(), mvm_sta, IWL_STA_AUTHORIZED, IWL_STA_ASSOC));
  ASSERT_EQ(mvm_sta->sta_state, iwl_sta_state::IWL_STA_ASSOC);
  ASSERT_EQ(ZX_OK, ClearAssoc());
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
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);

  // Call ClearAssoc() again to check if it is handled correctly.
  ASSERT_NE(ZX_OK, ClearAssoc());
}

// ClearAssoc() should cleanup when called after a failed Assoc
TEST_F(MacInterfaceTest, ClearAssocAfterFailedAssoc) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);

  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);
  // Fail the association by forcing some relevant internal state.
  auto orig = mvmvif_->uploaded;
  mvmvif_->uploaded = false;
  ASSERT_EQ(ZX_ERR_IO, ConfigureAssoc(&kAssocCtx));
  mvmvif_->uploaded = orig;

  // ClearAssoc will clean up the failed association.
  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);

  // Call ClearAssoc() again to check if it is handled correctly.
  ASSERT_NE(ZX_OK, ClearAssoc());
}

// This test case is to verify ConfigureAssoc() with HT wlan_assoc_ctx_t input can successfully
// trigger LQ_CMD with correct data.
TEST_F(MacInterfaceTest, AssocWithHtConfig) {
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));

  ExpectSendCmd(expected_cmd_id_list({
      MockCommand(WIDE_ID(LONG_GROUP, LQ_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, ADD_STA)),
      MockCommand(WIDE_ID(LONG_GROUP, MAC_CONTEXT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, TIME_EVENT_CMD)),
      MockCommand(WIDE_ID(LONG_GROUP, MCAST_FILTER_CMD)),
  }));

  // Extract LQ_CMD data.
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  struct iwl_lq_cmd* lq_cmd = &mvm_sta->lq_sta.rs_drv.lq;

  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kHtAssocCtx));

  // Verify the values in LQ_CMD API structure.
  EXPECT_EQ(lq_cmd->sta_id, 0);
  EXPECT_EQ(lq_cmd->reduced_tpc, 0);
  EXPECT_EQ(lq_cmd->flags, 0);
  EXPECT_EQ(lq_cmd->mimo_delim, 0);
  EXPECT_EQ(lq_cmd->single_stream_ant_msk, 1);
  EXPECT_EQ(lq_cmd->dual_stream_ant_msk, 3);
  EXPECT_EQ(lq_cmd->initial_rate_index[0], 0);
  EXPECT_EQ(lq_cmd->initial_rate_index[1], 0);
  EXPECT_EQ(lq_cmd->initial_rate_index[2], 0);
  EXPECT_EQ(lq_cmd->initial_rate_index[3], 0);
  EXPECT_EQ(lq_cmd->agg_time_limit, 0x0fa0);
  EXPECT_EQ(lq_cmd->agg_disable_start_th, 3);
  EXPECT_EQ(lq_cmd->agg_frame_cnt_limit, 1);
  EXPECT_EQ(lq_cmd->reserved2, 0);

  // Verify rate_n_flags in the table.
  EXPECT_EQ(lq_cmd->rs_table[0], 0x4103);
  // The value of RS_MNG_RETRY_TABLE_INITIAL_RATE_NUM is 3.
  EXPECT_EQ(lq_cmd->rs_table[3], 0x4102);

  EXPECT_EQ(lq_cmd->ss_params, 0);

  // Stop checking following commands one by one.
  ResetSendCmdFunc();

  // Clean up the association states.
  ASSERT_EQ(ZX_OK, ClearAssoc());
}

// Check to ensure keys are set during assoc and deleted after disassoc
// for now use open network
TEST_F(MacInterfaceTest, SetKeysTest) {
  constexpr uint8_t kIeeeOui[] = {0x00, 0x0F, 0xAC};
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  struct iwl_mvm_sta* mvm_sta = mvmvif_->mvm->fw_id_to_mac_id[mvmvif_->ap_sta_id];
  ASSERT_EQ(IWL_STA_NONE, mvm_sta->sta_state);
  struct iwl_mvm* mvm = mvmvif_->mvm;
  ASSERT_GT(list_length(&mvm->time_event_list), 0);

  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(IWL_STA_AUTHORIZED, mvm_sta->sta_state);
  ASSERT_EQ(true, mvmvif_->bss_conf.assoc);
  ASSERT_EQ(kListenInterval, mvmvif_->bss_conf.listen_interval);

  char keybuf[sizeof(wlan_key_config_t) + 16] = {};
  wlan_key_config_t* key_config = new (keybuf) wlan_key_config_t();

  // Set an arbitrary pairwise key.
  key_config->cipher_type = 4;
  key_config->key_type = 1;
  key_config->key_idx = 0;
  key_config->key_len = 16;
  memcpy(key_config->cipher_oui, kIeeeOui, 3);
  ASSERT_EQ(ZX_OK, SetKey(key_config));
  // Expect bit 0 to be set.
  ASSERT_EQ(*mvm->fw_key_table, 0x1);

  // Set an arbitrary group key.
  key_config->key_type = 2;
  key_config->key_idx = 1;
  ASSERT_EQ(ZX_OK, SetKey(key_config));
  // Expect bit 1 to be set as well.
  ASSERT_EQ(*mvm->fw_key_table, 0x3);
  ASSERT_EQ(ZX_OK, ClearAssoc());
  ASSERT_EQ(nullptr, mvmvif_->phy_ctxt);
  ASSERT_EQ(IWL_MVM_INVALID_STA, mvmvif_->ap_sta_id);
  ASSERT_EQ(list_length(&mvm->time_event_list), 0);
  // Both the keys should have been deleted.
  ASSERT_EQ(*mvm->fw_key_table, 0x0);
}

// Check that we can sucessfully set some key configurations required for supported functionality.
TEST_F(MacInterfaceTest, SetKeysSupportConfigs) {
  constexpr uint8_t kIeeeOui[] = {0x00, 0x0F, 0xAC};
  ASSERT_EQ(ZX_OK, SetChannel(&kChannel));
  ASSERT_EQ(ZX_OK, ConfigureBss(&kBssConfig));
  ASSERT_EQ(ZX_OK, ConfigureAssoc(&kAssocCtx));
  ASSERT_EQ(true, mvmvif_->bss_conf.assoc);

  char keybuf[sizeof(wlan_key_config_t) + 16] = {};
  wlan_key_config_t* key_config = new (keybuf) wlan_key_config_t();
  key_config->key_len = 16;
  memcpy(key_config->cipher_oui, kIeeeOui, 3);

  // Default cipher configuration for WPA2/3 PTK.  This is data frame protection, required for
  // WPA2/3.
  key_config->cipher_type = fuchsia_wlan_ieee80211_CipherSuiteType_CCMP_128;
  key_config->key_type = WLAN_KEY_TYPE_PAIRWISE;
  key_config->key_idx = 0;
  ASSERT_EQ(ZX_OK, SetKey(key_config));

  // Default cipher configuration for WPA2/3 IGTK.  This is management frame protection, optional
  // for WPA2 and required for WPA3.
  key_config->cipher_type = fuchsia_wlan_ieee80211_CipherSuiteType_BIP_CMAC_128;
  key_config->key_type = WLAN_KEY_TYPE_IGTK;
  key_config->key_idx = 1;
  ASSERT_EQ(ZX_OK, SetKey(key_config));

  ASSERT_EQ(ZX_OK, ClearAssoc());
}

TEST_F(MacInterfaceTest, TxPktTooLong) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build();
  wlan_pkt->wlan_pkt()->mac_frame_size = WLAN_MSDU_MAX_LEN + 1;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanSoftmacQueueTx(wlan_pkt->wlan_pkt()));
  unbindTx();
}

TEST_F(MacInterfaceTest, TxPktNotSupportedRole) {
  SetChannel(&kChannel);
  ConfigureBss(&kBssConfig);
  BIND_TEST(sim_trans_.iwl_trans());

  // Set to an unsupported role.
  mvmvif_->mac_role = WLAN_MAC_ROLE_AP;

  bindTx(tx_wrapper);
  WlanPktBuilder builder;
  std::shared_ptr<WlanPktBuilder::WlanPkt> wlan_pkt = builder.build();
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanSoftmacQueueTx(wlan_pkt->wlan_pkt()));
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
  ASSERT_EQ(ZX_OK, device_->WlanSoftmacQueueTx(wlan_pkt->wlan_pkt()));
  unbindTx();
}

}  // namespace
}  // namespace wlan::testing
