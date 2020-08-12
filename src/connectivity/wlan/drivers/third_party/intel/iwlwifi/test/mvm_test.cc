// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/default.h>
#include <lib/fake-bti/bti.h>
#include <lib/mock-function/mock-function.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/time-event.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan {
namespace testing {
namespace {

// Helper function to create a PHY context for the interface.
//
static void setup_phy_ctxt(struct iwl_mvm_vif* mvmvif) TA_NO_THREAD_SAFETY_ANALYSIS {
  // Create a PHY context and assign it to mvmvif.
  wlan_channel_t chandef = {
      // any arbitrary values
      .primary = 6,
  };
  uint16_t phy_ctxt_id;

  struct iwl_mvm* mvm = mvmvif->mvm;
  mtx_unlock(&mvm->mutex);
  ASSERT_EQ(ZX_OK, iwl_mvm_add_chanctx(mvm, &chandef, &phy_ctxt_id));
  mvmvif->phy_ctxt = &(mvm->phy_ctxts[phy_ctxt_id]);
  mtx_lock(&mvm->mutex);
}

class MvmTest : public SingleApTest {
 public:
  MvmTest() TA_NO_THREAD_SAFETY_ANALYSIS {
    mvm_ = iwl_trans_get_mvm(sim_trans_.iwl_trans());
    mvmvif_ = reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));
    mvmvif_->mvm = mvm_;
    mvmvif_->mac_role = WLAN_INFO_MAC_ROLE_CLIENT;
    mvmvif_->ifc.ops = reinterpret_cast<wlanmac_ifc_protocol_ops_t*>(
        calloc(1, sizeof(wlanmac_ifc_protocol_ops_t)));
    mvm_->mvmvif[0] = mvmvif_;
    mvm_->vif_count++;

    mtx_lock(&mvm_->mutex);
  }

  ~MvmTest() TA_NO_THREAD_SAFETY_ANALYSIS {
    free(mvmvif_->ifc.ops);
    free(mvmvif_);
    mtx_unlock(&mvm_->mutex);
  }

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
  // save the rx_info passed to MLME.  See TearDown() for cleanup logic related to this function.
  void MockRecv(wlan_rx_info_t* rx_info) {
    // TODO(fxbug.dev/43218): replace rxq->napi with interface instance so that we can map to
    // mvmvif.
    mvmvif_->ifc.ctx = rx_info;  // 'ctx' was used as 'wlanmac_ifc_protocol_t*', but we override it
                                 // with 'wlan_rx_info_t*'.
    mvmvif_->ifc.ops->recv = [](void* ctx, uint32_t flags, const void* data_buffer,
                                size_t data_size, const wlan_rx_info_t* info) {
      wlan_rx_info_t* rx_info = reinterpret_cast<wlan_rx_info_t*>(ctx);
      *rx_info = *info;
    };
  }

  struct iwl_mvm* mvm_;
  struct iwl_mvm_vif* mvmvif_;
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

///////////////////////////////////////////////////////////////////////////////
//                                  Scan Test
//
class ScanTest : public MvmTest {
 public:
  ScanTest() {
    // Fake callback registered to capture scan completion responses.
    ops.hw_scan_complete = [](void* ctx, const wlan_hw_scan_result_t* result) {
      struct ScanResult* sr = (struct ScanResult*)ctx;
      sr->sme_notified = true;
      sr->success = (result->code == WLAN_HW_SCAN_SUCCESS ? true : false);
    };

    mvmvif_sta.mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());
    mvmvif_sta.mac_role = WLAN_INFO_MAC_ROLE_CLIENT;
    mvmvif_sta.ifc.ops = &ops;
    mvmvif_sta.ifc.ctx = &scan_result;

    // This can be moved out or overridden when we add other scan types.
    scan_config.scan_type = WLAN_HW_SCAN_TYPE_PASSIVE;

    // Create scan timeout async loop.
    trans_ = sim_trans_.iwl_trans();
    ASSERT_OK(async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &trans_->loop));
    ASSERT_OK(async_loop_start_thread(trans_->loop, "iwlwifi-mvm-test-worker", NULL));
    mvm_->dispatcher = async_loop_get_dispatcher(trans_->loop);
    mvm_->scan_timeout_task.handler = iwl_mvm_scan_timeout;
    mvm_->scan_timeout_task.state = (async_state_t)ASYNC_STATE_INIT;
    mvm_->scan_timeout_delay = ZX_SEC(10);

    // Create fake FW scan response.
    buildRxcb(&rxb, &scan_notif, sizeof(scan_notif));
  }

  ~ScanTest() {
    async_loop_quit(trans_->loop);
    async_loop_join_threads(trans_->loop);
    free(trans_->loop);
  }

  struct iwl_trans* trans_;
  wlanmac_ifc_protocol_ops_t ops;
  struct iwl_mvm_vif mvmvif_sta;
  wlan_hw_scan_config_t scan_config{.num_channels = 4, .channels = {7, 1, 40, 136}};
  struct iwl_rx_cmd_buffer rxb;
  struct iwl_periodic_scan_complete scan_notif {
    .status = IWL_SCAN_OFFLOAD_COMPLETED,
  };

  // Structure to capture scan results.
  struct ScanResult {
    bool sme_notified;
    bool success;
  } scan_result;
};

// Tests scenario for a successful scan completion.
TEST_F(ScanTest, RegPassiveScanSuccess) TA_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(nullptr, mvm_->scan_vif);
  ASSERT_EQ(false, scan_result.sme_notified);
  ASSERT_EQ(false, scan_result.success);

  ASSERT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(&mvmvif_sta, mvm_->scan_vif);

  struct iwl_rx_packet* pkt =
      (struct iwl_rx_packet*)((char*)io_buffer_virt(&rxb._io_buf) + rxb._offset);
  struct iwl_periodic_scan_complete* scan_notif = (struct iwl_periodic_scan_complete*)pkt->data;
  ASSERT_EQ(scan_notif->status, IWL_SCAN_OFFLOAD_COMPLETED);

  // Call notify complete to simulate scan completion.
  mtx_unlock(&mvm_->mutex);
  iwl_mvm_rx_lmac_scan_complete_notif(mvm_, &rxb);
  mtx_lock(&mvm_->mutex);

  EXPECT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(true, scan_result.sme_notified);
  EXPECT_EQ(true, scan_result.success);
}

// Tests scenario where the scan request aborted / failed.
TEST_F(ScanTest, RegPassiveScanAborted) TA_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(nullptr, mvm_->scan_vif);

  ASSERT_EQ(false, scan_result.sme_notified);
  ASSERT_EQ(false, scan_result.success);
  ASSERT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(&mvmvif_sta, mvm_->scan_vif);

  // Set scan status to ABORTED so simulate a scan abort.
  struct iwl_rx_packet* pkt =
      (struct iwl_rx_packet*)((char*)io_buffer_virt(&rxb._io_buf) + rxb._offset);
  struct iwl_periodic_scan_complete* scan_notif = (struct iwl_periodic_scan_complete*)pkt->data;
  scan_notif->status = IWL_SCAN_OFFLOAD_ABORTED;

  // Call notify complete to simulate scan abort.
  mtx_unlock(&mvm_->mutex);
  iwl_mvm_rx_lmac_scan_complete_notif(mvm_, &rxb);
  mtx_lock(&mvm_->mutex);

  EXPECT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(true, scan_result.sme_notified);
  EXPECT_EQ(false, scan_result.success);
}

// Tests condition where scan completion timeouts out due to no response from FW.
TEST_F(ScanTest, RegPassiveScanTimeout) TA_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(nullptr, mvm_->scan_vif);

  ASSERT_EQ(false, scan_result.sme_notified);
  ASSERT_EQ(false, scan_result.success);
  ASSERT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(&mvmvif_sta, mvm_->scan_vif);

  // Do not call notify complete, instead invoke the timeout callback
  // to simulate a timeout event.
  mtx_unlock(&mvm_->mutex);
  iwl_mvm_scan_timeout(mvm_->dispatcher, &mvm_->scan_timeout_task, ZX_OK);
  mtx_lock(&mvm_->mutex);

  EXPECT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(true, scan_result.sme_notified);
  EXPECT_EQ(false, scan_result.success);
}

// Tests condition where timer is shutdown and there is no response from FW.
TEST_F(ScanTest, RegPassiveScanTimerShutdown) TA_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(nullptr, mvm_->scan_vif);

  ASSERT_EQ(false, scan_result.sme_notified);
  ASSERT_EQ(false, scan_result.success);
  ASSERT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(&mvmvif_sta, mvm_->scan_vif);

  // Do not call notify complete, and invoke the timeout callback with
  // status CANCELED. This simulates a timer shutdown while it is pending.
  mtx_unlock(&mvm_->mutex);
  iwl_mvm_scan_timeout(mvm_->dispatcher, &mvm_->scan_timeout_task, ZX_ERR_CANCELED);
  mtx_lock(&mvm_->mutex);

  // Ensure the state is such that no FW response or timeout has happened.
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(false, scan_result.sme_notified);
  EXPECT_EQ(false, scan_result.success);
}

// Tests condition where iwl_mvm_mac_stop() is invoked while timer is pending.
TEST_F(ScanTest, RegPassiveScanTimerMvmStop) TA_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(nullptr, mvm_->scan_vif);

  ASSERT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(&mvmvif_sta, mvm_->scan_vif);

  mtx_unlock(&mvm_->mutex);
  iwl_mvm_mac_stop(mvm_);
  mtx_lock(&mvm_->mutex);

  // The expectation is that iwl_mvm_mac_stop() would have cancelled the timer and it should
  // not be found now.
  EXPECT_EQ(ZX_ERR_NOT_FOUND, async_cancel_task(mvm_->dispatcher, &mvm_->scan_timeout_task));
}

// Tests condition where multiple calls to the scan API returns appropriate error.
TEST_F(ScanTest, RegPassiveScanParallel) TA_NO_THREAD_SAFETY_ANALYSIS {
  ASSERT_EQ(0, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  ASSERT_EQ(ZX_OK, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
  EXPECT_EQ(IWL_MVM_SCAN_REGULAR, mvm_->scan_status & IWL_MVM_SCAN_REGULAR);
  EXPECT_EQ(ZX_ERR_SHOULD_WAIT, iwl_mvm_reg_scan_start(&mvmvif_sta, &scan_config));
}

///////////////////////////////////////////////////////////////////////////////
//                             Time Event Test
//
class TimeEventTest : public MvmTest {
 public:
  TimeEventTest() {
    // In order to init the mvmvif_->time_event_data.id to TE_MAX.
    iwl_mvm_mac_ctxt_init(mvmvif_);
  }
};

TEST_F(TimeEventTest, NormalCase) {
  // wait_for_notif is true.
  ASSERT_EQ(ZX_OK, iwl_mvm_protect_session(mvm_, mvmvif_, 1, 2, 3, true));
  ASSERT_EQ(ZX_OK, iwl_mvm_stop_session_protection(mvmvif_));

  // wait_for_notif is false.
  ASSERT_EQ(ZX_OK, iwl_mvm_protect_session(mvm_, mvmvif_, 1, 2, 3, false));
  ASSERT_EQ(ZX_OK, iwl_mvm_stop_session_protection(mvmvif_));
}

///////////////////////////////////////////////////////////////////////////////
//                               Binding Test
//
class BindingTest : public MvmTest {
 public:
  BindingTest() { setup_phy_ctxt(mvmvif_); }
};

TEST_F(BindingTest, CheckArgs) {
  // Failed because phy_ctxt is unexpected.
  mvmvif_->phy_ctxt = NULL;
  ASSERT_EQ(ZX_ERR_BAD_STATE, iwl_mvm_binding_add_vif(mvmvif_));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, iwl_mvm_binding_remove_vif(mvmvif_));
}

TEST_F(BindingTest, NormalCase) {
  ASSERT_EQ(ZX_OK, iwl_mvm_binding_add_vif(mvmvif_));
  ASSERT_EQ(ZX_OK, iwl_mvm_binding_remove_vif(mvmvif_));
}

///////////////////////////////////////////////////////////////////////////////
//                               Power Test
//
class PowerTest : public MvmTest {
 public:
  PowerTest() { setup_phy_ctxt(mvmvif_); }
};

// By default, only one interface is created and its ps_disabled is false. So:
//
//   - mvmvif->pm_enabled is true.
//   - mvmvif->ps_disabled is false.
//   - thus, mvm->ps_disabled is false as well.
//
TEST_F(PowerTest, DefaultCase) {
  ASSERT_EQ(ZX_OK, iwl_mvm_power_update_mac(mvm_));
  EXPECT_EQ(true, mvmvif_->pm_enabled);
  EXPECT_EQ(false, mvmvif_->ps_disabled);
  EXPECT_EQ(false, mvm_->ps_disabled);
}

// Disable the PS of interface. We shall see MVM PS is disabled as well.
//
//   - mvmvif->pm_enabled is true.
//   - mvmvif->ps_disabled is false.
//   - thus, mvm->ps_disabled is false as well.
//
TEST_F(PowerTest, PsDisabled) {
  mvmvif_->ps_disabled = true;
  ASSERT_EQ(ZX_OK, iwl_mvm_power_update_mac(mvm_));
  EXPECT_EQ(true, mvmvif_->pm_enabled);
  EXPECT_EQ(true, mvmvif_->ps_disabled);
  EXPECT_EQ(true, mvm_->ps_disabled);
}

// The input pm_enabled has no effect since it is determined by iwl_mvm_power_update_mac() according
// to the current interface configuraiton.
//
// The expected results are identical to the default case above.
//
TEST_F(PowerTest, PmHasNoEffect) {
  mvmvif_->pm_enabled = false;
  ASSERT_EQ(ZX_OK, iwl_mvm_power_update_mac(mvm_));
  EXPECT_EQ(true, mvmvif_->pm_enabled);
  EXPECT_EQ(false, mvmvif_->ps_disabled);
  EXPECT_EQ(false, mvm_->ps_disabled);

  mvmvif_->pm_enabled = true;
  ASSERT_EQ(ZX_OK, iwl_mvm_power_update_mac(mvm_));
  EXPECT_EQ(true, mvmvif_->pm_enabled);
  EXPECT_EQ(false, mvmvif_->ps_disabled);
  EXPECT_EQ(false, mvm_->ps_disabled);
}

///////////////////////////////////////////////////////////////////////////////
//                               Txq Test
//
class TxqTest : public MvmTest {
 public:
  TxqTest()
      : sta_{
            .addr = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
            .sta_id = 0,
            .mvmvif = mvmvif_,
        } {
    for (size_t i = 0; i < ARRAY_SIZE(sta_.txq); ++i) {
      sta_.txq[i] = reinterpret_cast<struct iwl_mvm_txq*>(calloc(1, sizeof(struct iwl_mvm_txq)));
      ASSERT_NE(nullptr, sta_.txq[i]);
    }
  }

  ~TxqTest() {
    for (size_t i = 0; i < ARRAY_SIZE(sta_.txq); ++i) {
      free(sta_.txq[i]);
    }
  }

  struct iwl_mvm_sta sta_;
};

TEST_F(TxqTest, TestAllocManagement) {
  // Ensure the internal state is cleared.
  ASSERT_EQ(0, sta_.tid_data[IWL_MAX_TID_COUNT].txq_id);
  ASSERT_EQ(0, sta_.tfd_queue_msk);

  // Keep asking for queue for management packet (TID=MAX).
  // Expect txq_id IWL_MVM_DQA_MIN_MGMT_QUEUE is allocated.
  auto expected_mask = sta_.tfd_queue_msk;
  for (size_t i = 0; i < (IWL_MVM_DQA_MAX_MGMT_QUEUE - IWL_MVM_DQA_MIN_MGMT_QUEUE + 1); ++i) {
    int tid = IWL_MAX_TID_COUNT;
    ASSERT_EQ(ZX_OK, iwl_mvm_sta_alloc_queue(mvm_, &sta_, IEEE80211_AC_BE, tid));

    EXPECT_EQ(i + IWL_MVM_DQA_MIN_MGMT_QUEUE, sta_.tid_data[tid].txq_id);
    expected_mask |= BIT(i + IWL_MVM_DQA_MIN_MGMT_QUEUE);
    EXPECT_EQ(expected_mask, sta_.tfd_queue_msk);
  }

  // Request once more. Since there is no queue for management packet, expect data queue.
  ASSERT_EQ(ZX_OK, iwl_mvm_sta_alloc_queue(mvm_, &sta_, IEEE80211_AC_BE, IWL_MAX_TID_COUNT));
  EXPECT_EQ(IWL_MVM_DQA_MIN_DATA_QUEUE, sta_.tid_data[IWL_MAX_TID_COUNT].txq_id);
  expected_mask |= BIT(IWL_MVM_DQA_MIN_DATA_QUEUE);
  EXPECT_EQ(expected_mask, sta_.tfd_queue_msk);
}

TEST_F(TxqTest, TestAllocData) {
  // Ensure the internal state is cleared.
  ASSERT_EQ(0, sta_.tid_data[IWL_MAX_TID_COUNT].txq_id);
  ASSERT_EQ(0, sta_.tfd_queue_msk);

  // Keep asking for queue for data packet (TID!=MAX).
  // Expect txq_id IWL_MVM_DQA_MIN_DATA_QUEUE is allocated.
  auto expected_mask = sta_.tfd_queue_msk;
  for (size_t i = 0; i < (IWL_MVM_DQA_MAX_DATA_QUEUE - IWL_MVM_DQA_MIN_DATA_QUEUE + 1); ++i) {
    int tid = IWL_TID_NON_QOS;
    ASSERT_EQ(ZX_OK, iwl_mvm_sta_alloc_queue(mvm_, &sta_, IEEE80211_AC_BE, tid));

    EXPECT_EQ(i + IWL_MVM_DQA_MIN_DATA_QUEUE, sta_.tid_data[tid].txq_id);
    expected_mask |= BIT(i + IWL_MVM_DQA_MIN_DATA_QUEUE);
    EXPECT_EQ(expected_mask, sta_.tfd_queue_msk);
  }

  // Request once more. Since there is no queue for data packet, expect failure.
  // TODO(fxbug.dev/49530): this should be re-written once shared queue is supported.
  ASSERT_EQ(ZX_ERR_NO_RESOURCES, iwl_mvm_sta_alloc_queue(mvm_, &sta_, IEEE80211_AC_BE, 0));
}

}  // namespace
}  // namespace testing
}  // namespace wlan
