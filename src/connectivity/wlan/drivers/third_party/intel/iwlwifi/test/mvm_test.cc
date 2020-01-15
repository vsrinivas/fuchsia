// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/mock-function/mock-function.h>
#include <lib/zx/bti.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan {
namespace testing {
namespace {

class MvmTest : public SingleApTest {
 public:
  MvmTest() : mvm_(iwl_trans_get_mvm(sim_trans_.iwl_trans())) {}
  ~MvmTest() {}

 protected:
  void buildRxcb(struct iwl_rx_cmd_buffer* rxcb, void* pkt_data, size_t pkt_len) {
    io_buffer_t io_buf;
    zx::bti fake_bti;
    fake_bti_create(fake_bti.reset_and_get_address());
    io_buffer_init(&io_buf, fake_bti.get(), pkt_len + sizeof(struct iwl_rx_packet),
                   IO_BUFFER_RW | IO_BUFFER_CONTIG);
    rxcb->_io_buf = io_buf;
    rxcb->_offset = 0;

    struct iwl_rx_packet* pkt = reinterpret_cast<struct iwl_rx_packet*>(io_buffer_virt(&io_buf));
    // Most fields are not cared but initialized with known values.
    pkt->len_n_flags = cpu_to_le32(0);
    pkt->hdr.cmd = 0;
    pkt->hdr.group_id = 0;
    pkt->hdr.sequence = 0;
    memcpy(pkt->data, pkt_data, pkt_len);
  }

  // This function is kind of dirty. It hijacks the wlanmac_ifc_protocol_t.recv() so that we can
  // save the rx_info passed to MLME.
  void MockRecv(wlan_rx_info_t* rx_info) {
    // TODO(43218): replace rxq->napi with interface instance so that we can map to mvmvif.
    struct iwl_mvm_vif* mvmvif =
        reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));
    mvm_->mvmvif[0] = mvmvif;
    wlanmac_ifc_protocol_ops_t* ops = reinterpret_cast<wlanmac_ifc_protocol_ops_t*>(
        calloc(1, sizeof(wlanmac_ifc_protocol_ops_t)));
    mvmvif->ifc.ops = ops;
    mvmvif->ifc.ctx = rx_info;  // 'ctx' was used as 'wlanmac_ifc_protocol_t*', but we override it
                                // with 'wlan_rx_info_t*'.
    ops->recv = [](void* ctx, uint32_t flags, const void* data_buffer, size_t data_size,
                   const wlan_rx_info_t* info) {
      wlan_rx_info_t* rx_info = reinterpret_cast<wlan_rx_info_t*>(ctx);
      *rx_info = *info;
    };
  }

  struct iwl_mvm* mvm_;
};

TEST_F(MvmTest, GetMvm) { EXPECT_NE(mvm_, nullptr); }

TEST_F(MvmTest, rxMpdu) {
  const int kExpChan = 40;

  // Simulate the previous PHY_INFO packet
  struct iwl_rx_phy_info phy_info = {
      .non_cfg_phy_cnt = IWL_RX_INFO_ENERGY_ANT_ABC_IDX + 1,
      .phy_flags = cpu_to_le16(0),
      .channel = cpu_to_le16(kExpChan),
      .non_cfg_phy =
          {
              [IWL_RX_INFO_ENERGY_ANT_ABC_IDX] = 0x000a28,  // RSSI C:n/a B:-10, A:-40
          },
      .rate_n_flags = cpu_to_le32(0x7),  // IWL_RATE_18M_PLCP
  };
  struct iwl_rx_cmd_buffer phy_info_rxcb = {};
  buildRxcb(&phy_info_rxcb, &phy_info, sizeof(phy_info));
  iwl_mvm_rx_rx_phy_cmd(mvm_, &phy_info_rxcb);

  // Now, it comes the MPDU packet.
  const size_t kMacPayloadLen = 60;
  struct {
    struct iwl_rx_mpdu_res_start rx_res;
    struct ieee80211_frame_header frame;
    uint8_t mac_payload[kMacPayloadLen];
    uint32_t rx_pkt_status;
  } __packed mpdu = {
      .rx_res =
          {
              .byte_count = kMacPayloadLen,
              .assist = 0,
          },
      .frame = {},
      .rx_pkt_status = 0x0,
  };
  struct iwl_rx_cmd_buffer mpdu_rxcb = {};
  buildRxcb(&mpdu_rxcb, &mpdu, sizeof(mpdu));

  wlan_rx_info_t rx_info = {};
  MockRecv(&rx_info);
  iwl_mvm_rx_rx_mpdu(mvm_, nullptr /* napi */, &mpdu_rxcb);

  EXPECT_EQ(WLAN_RX_INFO_VALID_DATA_RATE, rx_info.valid_fields & WLAN_RX_INFO_VALID_DATA_RATE);
  EXPECT_EQ(TO_HALF_MBPS(18), rx_info.data_rate);
  EXPECT_EQ(kExpChan, rx_info.chan.primary);
  EXPECT_EQ(WLAN_RX_INFO_VALID_RSSI, rx_info.valid_fields & WLAN_RX_INFO_VALID_RSSI);
  EXPECT_EQ(static_cast<int8_t>(-10), rx_info.rssi_dbm);
}

// The antenna index will be toggled after each call.
// The current number of antenna is 1, which is determined by the sim-default-nvm.cc file.
TEST_F(MvmTest, toggleTxAntenna) {
  uint8_t ant = 0;

  // Since the current configuration is 1 antenna so that the 'ant' is always 0.
  iwl_mvm_toggle_tx_ant(mvm_, &ant);
  EXPECT_EQ(0, ant);
  iwl_mvm_toggle_tx_ant(mvm_, &ant);
  EXPECT_EQ(0, ant);
}

TEST_F(MvmTest, scanLmacErrorChecking) {
  struct iwl_mvm_scan_params params = {
      .n_scan_plans = IWL_MAX_SCHED_SCAN_PLANS + 1,
  };

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, iwl_mvm_scan_lmac(mvm_, &params));
}

// This test focuses on testing the scan_cmd filling.
TEST_F(MvmTest, scanLmacNormal) {
  ASSERT_NE(nullptr, mvm_->scan_cmd);  // scan cmd should have been allocated during init.

  struct iwl_mvm_scan_params params = {
      .type = IWL_SCAN_TYPE_WILD,
      .hb_type = IWL_SCAN_TYPE_NOT_SET,
      .n_channels = 4,
      .channels =
          {
              5,
              11,
              36,
              165,
          },
      .n_ssids = 0,
      .flags = 0,
      .pass_all = true,
      .n_match_sets = 0,
      .preq =
          {
              // arbitrary values for memory comparison below
              .mac_header =
                  {
                      .offset = cpu_to_le16(0x1234),
                      .len = cpu_to_le16(0x5678),
                  },
          },
      .n_scan_plans = 0,
  };

  EXPECT_EQ(ZX_OK, iwl_mvm_scan_lmac(mvm_, &params));
  struct iwl_scan_req_lmac* cmd = reinterpret_cast<struct iwl_scan_req_lmac*>(mvm_->scan_cmd);
  EXPECT_EQ(0x0001, le16_to_cpu(cmd->rx_chain_select));
  EXPECT_EQ(1, le32_to_cpu(cmd->iter_num));
  EXPECT_EQ(0, le32_to_cpu(cmd->delay));
  EXPECT_EQ(4, cmd->n_channels);
  EXPECT_EQ(PHY_BAND_24, le32_to_cpu(cmd->flags));
  EXPECT_EQ(1, cmd->schedule[0].iterations);
  struct iwl_scan_channel_cfg_lmac* channel_cfg =
      reinterpret_cast<struct iwl_scan_channel_cfg_lmac*>(cmd->data);
  EXPECT_EQ(5, le16_to_cpu(channel_cfg[0].channel_num));
  EXPECT_EQ(165, le16_to_cpu(channel_cfg[3].channel_num));
  // preq
  uint8_t* preq =
      &cmd->data[sizeof(struct iwl_scan_channel_cfg_lmac) * mvm_->fw->ucode_capa.n_scan_channels];
  EXPECT_EQ(0x34, preq[0]);
  EXPECT_EQ(0x12, preq[1]);
  EXPECT_EQ(0x78, preq[2]);
  EXPECT_EQ(0x56, preq[3]);
}

// This test focuses on testing the mvm state change for scan.
TEST_F(MvmTest, RegScanStartPassive) {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(nullptr, mvm_->scan_vif);

  struct iwl_mvm_vif mvmvif_sta = {
      .mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans()),
      .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
  };
  wlan_hw_scan_config_t scan_config = {
      .scan_type = WLAN_HW_SCAN_TYPE_PASSIVE,
      .num_channels = 4,
      .channels =
          {
              7,
              1,
              40,
              136,
          },
  };

  EXPECT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));

  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(&mvmvif_sta, mvm_->scan_vif);
}

}  // namespace
}  // namespace testing
}  // namespace wlan
