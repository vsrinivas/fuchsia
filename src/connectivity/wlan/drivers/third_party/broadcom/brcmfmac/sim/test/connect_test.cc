// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <zircon/errors.h>

#include <wifi/wifi-config.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fweh.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/drivers/wlanif/convert.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

// Some default AP and association request values
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
constexpr cssid_t kDefaultSsid = {.len = 15, .data = "Fuchsia Fake AP"};
const uint8_t kIes[] = {
    // SSID
    0x00, 0x0f, 'F', 'u', 'c', 'h', 's', 'i', 'a', ' ', 'F', 'a', 'k', 'e', ' ', 'A', 'P',
    // Supported rates
    0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
    // DS parameter set - channel 157
    0x03, 0x01, 0x9d,
    // DTIM
    0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
    // Power constraint
    0x20, 0x01, 0x03,
    // HT capabilities
    0x2d, 0x1a, 0xef, 0x09, 0x1b, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // HT operation
    0x3d, 0x16, 0x9d, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Overlapping BSS scan parameters
    0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
    // Extended capabilities
    0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
    // VHT capabilities
    0xbf, 0x0c, 0xb2, 0x01, 0x80, 0x33, 0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00,
    // VHT operation
    0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
    // VHT Tx power envelope
    0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
    // Vendor IE - WMM parameters
    0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4,
    0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00,
    // Vendor IE - Atheros advanced capability
    0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
    // RSN
    0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
    0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
    // Vendor IE - WPS
    0xdd, 0x1d, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01, 0x02,
    0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
const common::MacAddr kMadeupClient({0xde, 0xad, 0xbe, 0xef, 0x00, 0x01});
constexpr auto kDefaultApDisassocReason = wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON;
constexpr auto kDefaultApDeauthReason = wlan_ieee80211::ReasonCode::INVALID_AUTHENTICATION;
constexpr auto kDefaultClientDeauthReason = wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC;
// Sim firmware returns these values for SNR and RSSI.
const uint8_t kDefaultSimFwSnr = 40;
const int8_t kDefaultSimFwRssi = -20;

class ConnectTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();

  void StartDisassoc();
  void DisassocFromAp();

  // Run through the connect flow
  void StartConnect();
  void StartReconnect();

  // Send bad association responses
  void SendBadResp();

  // Send repeated association responses
  void SendMultipleResp();

  // Send association response with WMM IE
  void SendAssocRespWithWmm();

  // Send one authentication response to help client passing through authentication process
  void SendOpenAuthResp();

  // Send Disassociate request to SIM FW
  void DisassocClient(const common::MacAddr& mac_addr);

  // Pretend to transmit Disassoc from AP
  void TxFakeDisassocReq();

  // Deauth routines
  void StartDeauth();
  void DeauthClient();
  void DeauthFromAp();

  void ConnectErrorInject();
  void ConnectErrorEventInject(brcmf_fweh_event_status_t ret_status, status_code_t ret_reason);

  void GetIfaceCounterStats(wlan_fullmac_iface_counter_stats_t* out_stats);
  void GetIfaceHistogramStats(wlan_fullmac_iface_histogram_stats_t* out_stats);
  void DetailedHistogramErrorInject();

 protected:
  struct ConnectContext {
    // Information about the BSS we are attempting to associate with. Used to generate the
    // appropriate MLME calls (Join => Auth => Assoc).
    simulation::WlanTxInfo tx_info = {.channel = kDefaultChannel};
    common::MacAddr bssid = kDefaultBssid;
    cssid_t ssid = kDefaultSsid;
    std::vector<uint8_t> ies = std::vector<uint8_t>(kIes, kIes + sizeof(kIes));

    // There should be one result for each association response received
    std::list<status_code_t> expected_results;
    std::vector<uint8_t> expected_wmm_param;

    // An optional function to call when we see the association request go out.
    std::function<void()> on_assoc_req_callback;
    // An optional function to call when we see the authentication request go out.
    std::function<void()> on_auth_req_callback;

    // Track number of connection responses
    size_t connect_resp_count = 0;
    // Track number of disassociation confs (initiated from self)
    size_t disassoc_conf_count = 0;
    // Track number of disassoc indications (initiated from AP)
    size_t disassoc_ind_count = 0;
    // Track number of deauth indications (initiated from AP)
    size_t deauth_ind_count = 0;
    // Number of deauth confirmations (when initiated by self)
    size_t deauth_conf_count = 0;
    // Whether deauth/disassoc is locally initiated
    size_t ind_locally_initiated_count = 0;
    // Number of signal report indications (once client is assoc'd)
    size_t signal_ind_count = 0;
    // SNR seen in the signal report indication.
    int16_t signal_ind_snr = 0;
    // RSSI seen in the signal report indication.
    int16_t signal_ind_rssi = 0;
  };

  struct AssocRespInfo {
    wlan_channel_t channel;
    common::MacAddr src;
    common::MacAddr dst;
    wlan_ieee80211::StatusCode status;
  };

  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;

  ConnectContext context_;

  // Keep track of the APs that are in operation so we can easily disable beaconing on all of them
  // at the end of each test.
  std::list<simulation::FakeAp*> aps_;

  // All of the association responses seen in the environment.
  std::list<AssocRespInfo> assoc_responses_;

  // All the reason codes of de-authentication frames seen in the environment.
  std::list<wlan_ieee80211::ReasonCode> deauth_frames_;

  // All the status codes of authentication responses seen in the environment.
  std::list<wlan_ieee80211::StatusCode> auth_resp_status_list_;

  // Trigger to start disassociation. If set to true, disassociation is started
  // soon after association completes.
  bool start_disassoc_ = false;
  // If disassoc_from_ap_ is set to true, the disassociation process is started
  // from the FakeAP else from the station itself.
  bool disassoc_from_ap_ = false;

  // This flag is checked only if disassoc_from_ap_ is false. If set to true, the
  // local client mac is used in disassoc_req else a fake mac address is used
  bool disassoc_self_ = true;

  // Indicates if deauth needs to be issued.
  bool start_deauth_ = false;
  // Indicates if deauth is from the AP or from self
  bool deauth_from_ap_ = false;
  // Indicates reconnect (via assoc) needs to be scheduled when disassoc_ind is received.
  bool start_reconnect_assoc_ = false;
  // Indicates reconnect (via assoc) needs to be issued right when disassoc_ind is received.
  bool start_reconnect_assoc_instant_ = false;

 private:
  // StationIfc overrides
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;

  // SME callbacks
  static wlan_fullmac_impl_ifc_protocol_ops_t sme_ops_;
  wlan_fullmac_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnConnectConf(const wlan_fullmac_connect_confirm_t* resp);
  void OnDisassocInd(const wlan_fullmac_disassoc_indication_t* ind);
  void OnDisassocConf(const wlan_fullmac_disassoc_confirm_t* resp);
  void OnDeauthConf(const wlan_fullmac_deauth_confirm_t* resp);
  void OnDeauthInd(const wlan_fullmac_deauth_indication_t* ind);
  void OnSignalReport(const wlan_fullmac_signal_report_indication* ind);
};

// Since we're acting as wlan_fullmac, we need handlers for any protocol calls we may receive
wlan_fullmac_impl_ifc_protocol_ops_t ConnectTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlan_fullmac_scan_result_t* result) {
          // Ignore
        },
    .on_scan_end =
        [](void* cookie, const wlan_fullmac_scan_end_t* end) {
          // Ignore
        },
    .connect_conf =
        [](void* cookie, const wlan_fullmac_connect_confirm_t* resp) {
          static_cast<ConnectTest*>(cookie)->OnConnectConf(resp);
        },
    .deauth_conf =
        [](void* cookie, const wlan_fullmac_deauth_confirm_t* resp) {
          static_cast<ConnectTest*>(cookie)->OnDeauthConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlan_fullmac_deauth_indication_t* ind) {
          static_cast<ConnectTest*>(cookie)->OnDeauthInd(ind);
        },
    .disassoc_conf =
        [](void* cookie, const wlan_fullmac_disassoc_confirm_t* resp) {
          static_cast<ConnectTest*>(cookie)->OnDisassocConf(resp);
        },
    .disassoc_ind =
        [](void* cookie, const wlan_fullmac_disassoc_indication_t* ind) {
          static_cast<ConnectTest*>(cookie)->OnDisassocInd(ind);
        },
    .signal_report =
        [](void* cookie, const wlan_fullmac_signal_report_indication* ind) {
          static_cast<ConnectTest*>(cookie)->OnSignalReport(ind);
        },
};

void ConnectTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                     std::shared_ptr<const simulation::WlanRxInfo> info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
  // If a handler has been installed, call it
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ) {
    if (context_.on_assoc_req_callback) {
      env_->ScheduleNotification(context_.on_assoc_req_callback, zx::msec(1));
    }
  }

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP) {
    auto assoc_resp = std::static_pointer_cast<const simulation::SimAssocRespFrame>(mgmt_frame);
    AssocRespInfo resp_info = {.channel = info->channel,
                               .src = assoc_resp->src_addr_,
                               .dst = assoc_resp->dst_addr_,
                               .status = assoc_resp->status_};
    assoc_responses_.push_back(resp_info);
  }

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_AUTH) {
    auto auth_frame = std::static_pointer_cast<const simulation::SimAuthFrame>(mgmt_frame);
    // When we receive a authentication request, try to call the callback.
    if (auth_frame->seq_num_ == 1) {
      if (context_.on_auth_req_callback) {
        env_->ScheduleNotification(context_.on_auth_req_callback, zx::msec(1));
      }
      return;
    }

    if (auth_frame->seq_num_ == 2 || auth_frame->seq_num_ == 4)
      auth_resp_status_list_.push_back(auth_frame->status_);
  }

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_DEAUTH) {
    auto deauth_frame = std::static_pointer_cast<const simulation::SimDeauthFrame>(mgmt_frame);
    deauth_frames_.push_back(deauth_frame->reason_);
  }
}

// Create our device instance and hook up the callbacks
void ConnectTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc_, &sme_protocol_), ZX_OK);
  context_.connect_resp_count = 0;
  context_.disassoc_conf_count = 0;
  context_.deauth_ind_count = 0;
  context_.disassoc_ind_count = 0;
  context_.ind_locally_initiated_count = 0;
  context_.signal_ind_count = 0;
  context_.signal_ind_rssi = 0;
  context_.signal_ind_snr = 0;

  // Reset all of these settings, which should be set in tests that need them.
  start_disassoc_ = false;
  disassoc_from_ap_ = false;
  disassoc_self_ = true;
  start_deauth_ = false;
  deauth_from_ap_ = false;
}

void ConnectTest::DisassocFromAp() {
  common::MacAddr my_mac;
  client_ifc_.GetMacAddr(&my_mac);

  // Disassoc the STA
  for (auto ap : aps_) {
    ap->DisassocSta(my_mac, kDefaultApDisassocReason);
  }
}

void ConnectTest::OnConnectConf(const wlan_fullmac_connect_confirm_t* resp) {
  context_.connect_resp_count++;
  EXPECT_EQ(resp->result_code, context_.expected_results.front());

  if (!context_.expected_wmm_param.empty()) {
    EXPECT_GT(resp->association_ies_count, 0ul);
    bool contains_wmm_param = false;
    for (size_t offset = 0;
         offset <= resp->association_ies_count - context_.expected_wmm_param.size(); offset++) {
      if (memcmp(resp->association_ies_list + offset, &context_.expected_wmm_param[0],
                 context_.expected_wmm_param.size()) == 0) {
        contains_wmm_param = true;
        break;
      }
    }
    EXPECT_TRUE(contains_wmm_param);
  }

  context_.expected_results.pop_front();
  context_.expected_wmm_param.clear();

  if (start_disassoc_) {
    env_->ScheduleNotification([this] { StartDisassoc(); }, zx::msec(200));
  } else if (start_deauth_) {
    env_->ScheduleNotification([this] { StartDeauth(); }, zx::msec(200));
  }
}

void ConnectTest::OnDisassocConf(const wlan_fullmac_disassoc_confirm_t* resp) {
  if (resp->status == ZX_OK) {
    context_.disassoc_conf_count++;
  }
}

void ConnectTest::OnDeauthConf(const wlan_fullmac_deauth_confirm_t* resp) {
  context_.deauth_conf_count++;
}

void ConnectTest::OnDeauthInd(const wlan_fullmac_deauth_indication_t* ind) {
  context_.deauth_ind_count++;
  if (ind->locally_initiated) {
    context_.ind_locally_initiated_count++;
  }
  client_ifc_.stats_.deauth_indications.push_back(*ind);
}

void ConnectTest::OnDisassocInd(const wlan_fullmac_disassoc_indication_t* ind) {
  context_.disassoc_ind_count++;
  if (ind->locally_initiated) {
    context_.ind_locally_initiated_count++;
  }
  client_ifc_.stats_.disassoc_indications.push_back(*ind);
  if (start_reconnect_assoc_instant_) {
    StartReconnect();
  } else if (start_reconnect_assoc_) {
    env_->ScheduleNotification(std::bind(&ConnectTest::StartReconnect, this), zx::sec(3));
  }
}

void ConnectTest::OnSignalReport(const wlan_fullmac_signal_report_indication* ind) {
  context_.signal_ind_count++;
  context_.signal_ind_rssi = ind->rssi_dbm;
  context_.signal_ind_snr = ind->snr_db;
}

void ConnectTest::StartConnect() {
  // Send connect request
  wlan_fullmac_connect_req connect_req = {};
  std::memcpy(connect_req.selected_bss.bssid, context_.bssid.byte, ETH_ALEN);
  connect_req.selected_bss.ies_list = context_.ies.data();
  connect_req.selected_bss.ies_count = context_.ies.size();
  connect_req.selected_bss.channel = context_.tx_info.channel;
  connect_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  connect_req.connect_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  client_ifc_.if_impl_ops_->connect_req(client_ifc_.if_impl_ctx_, &connect_req);
}

void ConnectTest::StartReconnect() {
  // Send reconnect request
  // This is what SME does on a disassoc ind.
  wlan_fullmac_reconnect_req reconnect_req = {};
  std::memcpy(reconnect_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->reconnect_req(client_ifc_.if_impl_ctx_, &reconnect_req);
}

// Verify that we get a signal report when associated.
TEST_F(ConnectTest, SignalReportTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  EXPECT_EQ((int64_t)context_.signal_ind_count,
            kTestDuration.get() / BRCMF_SIGNAL_REPORT_TIMER_DUR_MS);
  // Verify the plumbing between the firmware and the signal report.
  EXPECT_EQ(context_.signal_ind_snr, kDefaultSimFwSnr);
  EXPECT_EQ(context_.signal_ind_rssi, kDefaultSimFwRssi);
}

void ConnectTest::GetIfaceCounterStats(wlan_fullmac_iface_counter_stats_t* out_stats) {
  client_ifc_.if_impl_ops_->get_iface_counter_stats(client_ifc_.if_impl_ctx_, out_stats);
}

void ConnectTest::GetIfaceHistogramStats(wlan_fullmac_iface_histogram_stats_t* out_stats) {
  client_ifc_.if_impl_ops_->get_iface_histogram_stats(client_ifc_.if_impl_ctx_, out_stats);
}

TEST_F(ConnectTest, GetIfaceCounterStatsTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  wlan_fullmac_iface_counter_stats_t stats = {};

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::GetIfaceCounterStats, this, &stats),
                             zx::msec(30));

  env_->Run(kTestDuration);

  // Sim firmware returns these fake values for packet counters.
  const uint64_t fw_rx_good = 5;
  const uint64_t fw_rx_bad = 4;
  const uint64_t fw_rx_multicast = 1;
  const uint64_t fw_tx_good = 3;
  const uint64_t fw_tx_bad = 2;
  EXPECT_EQ(stats.rx_unicast_total, fw_rx_good + fw_rx_bad);
  EXPECT_EQ(stats.rx_unicast_drop, fw_rx_bad);
  EXPECT_EQ(stats.rx_multicast, fw_rx_multicast);
  EXPECT_EQ(stats.tx_total, fw_tx_good + fw_tx_bad);
  EXPECT_EQ(stats.tx_drop, fw_tx_bad);
}

TEST_F(ConnectTest, GetIfaceHistogramStatsTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  wlan_fullmac_iface_histogram_stats_t banjo_stats = {};

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::GetIfaceHistogramStats, this, &banjo_stats),
                             zx::msec(30));

  env_->Run(kTestDuration);

  fuchsia::wlan::stats::IfaceHistogramStats stats;
  wlanif::ConvertIfaceHistogramStats(&stats, banjo_stats);

  // Sim firmware returns these fake values for per-antenna histograms.
  const auto& expected_hist_scope = fuchsia::wlan::stats::HistScope::PER_ANTENNA;
  const auto& expected_antenna_freq = fuchsia::wlan::stats::AntennaFreq::ANTENNA_2_G;
  const uint8_t expected_antenna_index = 0;
  const uint8_t expected_snr_index = 60;
  const uint8_t expected_snr_num_frames = 50;
  // TODO(fxbug.dev/29698): Test all bucket values when sim firmware fully supports wstats_counters.
  // Sim firmware populates only SNR buckets, probably due to the discrepancies between the iovar
  // get handling between real and sim firmware (e.g. fxr/404141). When wstats_counters is fully
  // supported in sim firmware we can test for the expected noise floor, RSSI, and rate buckets.

  ASSERT_EQ(stats.noise_floor_histograms.size(), 1);
  EXPECT_EQ(stats.noise_floor_histograms[0].hist_scope, expected_hist_scope);
  ASSERT_NOT_NULL(stats.noise_floor_histograms[0].antenna_id);
  EXPECT_EQ(stats.noise_floor_histograms[0].antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(stats.noise_floor_histograms[0].antenna_id->index, expected_antenna_index);

  ASSERT_EQ(stats.rssi_histograms.size(), 1);
  EXPECT_EQ(stats.rssi_histograms[0].hist_scope, expected_hist_scope);
  ASSERT_NOT_NULL(stats.rssi_histograms[0].antenna_id);
  EXPECT_EQ(stats.rssi_histograms[0].antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(stats.rssi_histograms[0].antenna_id->index, expected_antenna_index);

  ASSERT_EQ(stats.rx_rate_index_histograms.size(), 1);
  EXPECT_EQ(stats.rx_rate_index_histograms[0].hist_scope, expected_hist_scope);
  ASSERT_NOT_NULL(stats.rx_rate_index_histograms[0].antenna_id);
  EXPECT_EQ(stats.rx_rate_index_histograms[0].antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(stats.rx_rate_index_histograms[0].antenna_id->index, expected_antenna_index);

  ASSERT_EQ(stats.snr_histograms.size(), 1);
  EXPECT_EQ(stats.snr_histograms[0].hist_scope, expected_hist_scope);
  ASSERT_NOT_NULL(stats.snr_histograms[0].antenna_id);
  EXPECT_EQ(stats.snr_histograms[0].antenna_id->freq, expected_antenna_freq);
  EXPECT_EQ(stats.snr_histograms[0].antenna_id->index, expected_antenna_index);
  ASSERT_EQ(stats.snr_histograms[0].snr_samples.size(), 1);
  EXPECT_EQ(stats.snr_histograms[0].snr_samples[0].bucket_index, expected_snr_index);
  EXPECT_EQ(stats.snr_histograms[0].snr_samples[0].num_samples, expected_snr_num_frames);
}

TEST_F(ConnectTest, GetIfaceHistogramStatsNotSupportedTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  wlan_fullmac_iface_histogram_stats_t banjo_stats = {};

  DetailedHistogramErrorInject();
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::GetIfaceHistogramStats, this, &banjo_stats),
                             zx::msec(30));

  env_->Run(kTestDuration);

  fuchsia::wlan::stats::IfaceHistogramStats stats;
  wlanif::ConvertIfaceHistogramStats(&stats, banjo_stats);

  ASSERT_TRUE(stats.noise_floor_histograms.empty());
  ASSERT_TRUE(stats.rssi_histograms.empty());
  ASSERT_TRUE(stats.rx_rate_index_histograms.empty());
  ASSERT_TRUE(stats.snr_histograms.empty());
}

void ConnectTest::ConnectErrorInject() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_OK, BCME_OK, client_ifc_.iface_id_);
}

void ConnectTest::ConnectErrorEventInject(brcmf_fweh_event_status_t ret_status,
                                          status_code_t ret_reason) {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrEventInjCmd(BRCMF_C_SET_SSID, BRCMF_E_ASSOC, ret_status, ret_reason,
                                          0, client_ifc_.iface_id_);
}

void ConnectTest::StartDisassoc() {
  // Send disassoc request
  if (!disassoc_from_ap_) {
    if (disassoc_self_) {
      DisassocClient(context_.bssid);
    } else {
      DisassocClient(kMadeupClient);
    }
  } else {
    DisassocFromAp();
  }
}

void ConnectTest::StartDeauth() {
  // Send deauth request
  if (!deauth_from_ap_) {
    DeauthClient();
  } else {
    // Send deauth frame
    DeauthFromAp();
  }
}

void ConnectTest::DisassocClient(const common::MacAddr& mac_addr) {
  wlan_fullmac_disassoc_req disassoc_req = {};

  std::memcpy(disassoc_req.peer_sta_address, mac_addr.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->disassoc_req(client_ifc_.if_impl_ctx_, &disassoc_req);
}

void ConnectTest::DeauthClient() {
  wlan_fullmac_deauth_req_t deauth_req = {
      .reason_code = static_cast<reason_code_t>(kDefaultClientDeauthReason)};

  std::memcpy(deauth_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->deauth_req(client_ifc_.if_impl_ctx_, &deauth_req);
}

void ConnectTest::DeauthFromAp() {
  // Figure out our own MAC
  common::MacAddr my_mac;
  client_ifc_.GetMacAddr(&my_mac);

  // Send a Deauth to our STA
  simulation::SimDeauthFrame deauth_frame(context_.bssid, my_mac, kDefaultApDeauthReason);
  env_->Tx(deauth_frame, context_.tx_info, this);
}

void ConnectTest::TxFakeDisassocReq() {
  // Figure out our own MAC
  common::MacAddr my_mac;
  client_ifc_.GetMacAddr(&my_mac);

  // Send a Disassoc Req to our STA (which is not associated)
  simulation::SimDisassocReqFrame not_associated_frame(context_.bssid, my_mac,
                                                       kDefaultApDisassocReason);
  env_->Tx(not_associated_frame, context_.tx_info, this);

  // Send a Disassoc Req from the wrong bss
  common::MacAddr wrong_src(context_.bssid);
  wrong_src.byte[ETH_ALEN - 1]++;
  simulation::SimDisassocReqFrame wrong_bss_frame(wrong_src, my_mac, kDefaultApDisassocReason);
  env_->Tx(wrong_bss_frame, context_.tx_info, this);

  // Send a Disassoc Req to a different STA
  common::MacAddr wrong_dst(my_mac);
  wrong_dst.byte[ETH_ALEN - 1]++;
  simulation::SimDisassocReqFrame wrong_sta_frame(context_.bssid, wrong_dst,
                                                  kDefaultApDisassocReason);
  env_->Tx(wrong_sta_frame, context_.tx_info, this);
}

void ConnectTest::DetailedHistogramErrorInject() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("wstats_counters", ZX_ERR_NOT_SUPPORTED, BCME_OK,
                                       client_ifc_.iface_id_);
}

// For this test, we want the pre-assoc scan test to fail because no APs are found.
TEST_F(ConnectTest, NoAps) {
  // Create our device instance
  Init();

  const common::MacAddr kBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
  context_.bssid = kBssid;
  context_.expected_results.push_front(STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);
  context_.ssid = {.len = 6, .data = "TestAP"};
  context_.tx_info.channel = {.primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that we can successfully associate to a fake AP
TEST_F(ConnectTest, SimpleTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ((int64_t)context_.signal_ind_count,
            kTestDuration.get() / BRCMF_SIGNAL_REPORT_TIMER_DUR_MS);
}

// Verify that we can successfully associate to a fake AP using the new connect API.
TEST_F(ConnectTest, SimpleConnectTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification([this] { StartConnect(); }, zx::msec(10));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ((int64_t)context_.signal_ind_count,
            kTestDuration.get() / BRCMF_SIGNAL_REPORT_TIMER_DUR_MS);
}

// Verify that we can associate using only SSID, not BSSID
TEST_F(ConnectTest, SsidTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that APs with incorrect SSIDs or BSSIDs are ignored
TEST_F(ConnectTest, WrongIds) {
  // Create our device instance
  Init();

  constexpr wlan_channel_t kWrongChannel = {
      .primary = 8, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
  ASSERT_NE(kDefaultChannel.primary, kWrongChannel.primary);
  constexpr cssid_t kWrongSsid = {.len = 14, .data = "Fuchsia Fake AP"};
  ASSERT_NE(kDefaultSsid.len, kWrongSsid.len);
  const common::MacAddr kWrongBssid({0x12, 0x34, 0x56, 0x78, 0x9b, 0xbc});
  ASSERT_NE(kDefaultBssid, kWrongBssid);

  // Start up fake APs
  simulation::FakeAp ap1(env_.get(), kDefaultBssid, kDefaultSsid, kWrongChannel);
  aps_.push_back(&ap1);
  simulation::FakeAp ap2(env_.get(), kWrongBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap2);
  simulation::FakeAp ap3(env_.get(), kDefaultBssid, kWrongSsid, kDefaultChannel);
  aps_.push_back(&ap3);

  context_.expected_results.push_front(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  // The APs aren't giving us a response, but the driver is telling us that the operation failed
  // because it couldn't find a matching AP.
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Attempt to associate while already associated
TEST_F(ConnectTest, RepeatedConnectTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  // The associations at 11ms and 12ms should be immediately refused (because there is already an
  // association in progress), and eventually the association that was in progress should succeed.
  context_.expected_results.push_back(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
  context_.expected_results.push_back(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
  context_.expected_results.push_back(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(11));
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(12));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 3U);
}

// Verify that if an AP does not respond to an association response we return a failure
TEST_F(ConnectTest, ApIgnoredRequest) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  // Make sure no responses were sent back from the fake AP
  EXPECT_EQ(assoc_responses_.size(), 0U);

  // But we still got our response from the driver
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that if an AP refuses an association request we return a temporary refusal.  The driver
// will also send a BRCMF_C_DISASSOC to clear AP state each time when refused.
TEST_F(ConnectTest, ApTemporarilyRefusedRequest) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_REFUSED_TEMPORARILY);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REFUSED_TEMPORARILY);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  uint32_t max_assoc_retries;
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &max_assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_EQ(max_assoc_retries, kMaxAssocRetries);
  // We should have gotten a refusal from the fake AP
  EXPECT_EQ(assoc_responses_.size(), max_assoc_retries + 1);
  EXPECT_EQ(assoc_responses_.front().status, wlan_ieee80211::StatusCode::REFUSED_TEMPORARILY);

  // Make sure we got our response from the driver
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that if an AP refuses an association request we return a failure.  The driver will also
// send a BRCMF_C_DISASSOC to clear AP state each time when refused.
TEST_F(ConnectTest, ApRefusedRequest) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_REFUSED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  uint32_t max_assoc_retries;
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &max_assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_EQ(max_assoc_retries, kMaxAssocRetries);

  // We should have gotten a refusal from the fake AP.
  EXPECT_EQ(assoc_responses_.size(), max_assoc_retries + 1);
  EXPECT_EQ(assoc_responses_.front().status,
            wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);

  // The AP should have received 1 deauth, no matter there were how many firmware assoc retries.
  EXPECT_EQ(deauth_frames_.size(), 1U);
  EXPECT_EQ(deauth_frames_.front(), wlan_ieee80211::ReasonCode::STA_LEAVING);
  // Make sure we got our response from the driver
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// SIM FW ignore client assoc request. Note that currently there is no timeout
// mechanism in the driver to handle this situation. It is currently being
// worked on.
TEST_F(ConnectTest, SimFwIgnoreConnectReq) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_back(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  ConnectErrorInject();
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(50));
  env_->Run(kTestDuration);

  // We should not have received a assoc response from SIM FW
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

void ConnectTest::SendBadResp() {
  // Figure out our own MAC
  common::MacAddr my_mac;
  client_ifc_.GetMacAddr(&my_mac);

  // Send a response from the wrong bss
  common::MacAddr wrong_src(context_.bssid);
  wrong_src.byte[ETH_ALEN - 1]++;
  simulation::SimAssocRespFrame wrong_bss_frame(wrong_src, my_mac,
                                                wlan_ieee80211::StatusCode::SUCCESS);
  env_->Tx(wrong_bss_frame, context_.tx_info, this);

  // Send a response to a different STA
  common::MacAddr wrong_dst(my_mac);
  wrong_dst.byte[ETH_ALEN - 1]++;
  simulation::SimAssocRespFrame wrong_dst_frame(context_.bssid, wrong_dst,
                                                wlan_ieee80211::StatusCode::SUCCESS);
  env_->Tx(wrong_dst_frame, context_.tx_info, this);
}

// Verify that any non-applicable association responses (i.e., sent to or from the wrong MAC)
// are ignored
TEST_F(ConnectTest, IgnoreRespMismatch) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // We want the association request to be ignored so we can inject responses and verify that
  // they are being ignored.
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);
  context_.on_assoc_req_callback = std::bind(&ConnectTest::SendBadResp, this);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  // Make sure that the firmware/driver ignored bad responses and sent back its own (failure)
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

void ConnectTest::SendMultipleResp() {
  constexpr unsigned kRespCount = 100;

  // Figure out our own MAC
  common::MacAddr my_mac;
  client_ifc_.GetMacAddr(&my_mac);
  simulation::SimAssocRespFrame multiple_resp_frame(context_.bssid, my_mac,
                                                    wlan_ieee80211::StatusCode::SUCCESS);
  for (unsigned i = 0; i < kRespCount; i++) {
    env_->Tx(multiple_resp_frame, context_.tx_info, this);
  }
}

void ConnectTest::SendAssocRespWithWmm() {
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", mac_buf, ETH_ALEN, nullptr);
  EXPECT_EQ(status, ZX_OK);
  common::MacAddr my_mac(mac_buf);
  simulation::SimAssocRespFrame assoc_resp_frame(context_.bssid, my_mac,
                                                 wlan_ieee80211::StatusCode::SUCCESS);

  uint8_t raw_ies[] = {
      // WMM param
      0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,  // WMM header
      0x80,                                            // Qos Info - U-ASPD enabled
      0x00,                                            // reserved
      0x03, 0xa4, 0x00, 0x00,                          // Best effort AC params
      0x27, 0xa4, 0x00, 0x00,                          // Background AC params
      0x42, 0x43, 0x5e, 0x00,                          // Video AC params
      0x62, 0x32, 0x2f, 0x00,                          // Voice AC params
  };
  assoc_resp_frame.AddRawIes(cpp20::span(raw_ies, sizeof(raw_ies)));

  env_->Tx(assoc_resp_frame, context_.tx_info, this);
}

void ConnectTest::SendOpenAuthResp() {
  common::MacAddr my_mac;
  client_ifc_.GetMacAddr(&my_mac);
  simulation::SimAuthFrame auth_resp(context_.bssid, my_mac, 2, simulation::AUTH_TYPE_OPEN,
                                     wlan_ieee80211::StatusCode::SUCCESS);
  env_->Tx(auth_resp, context_.tx_info, this);
}

// Verify that responses after association are ignored
TEST_F(ConnectTest, IgnoreExtraResp) {
  // Create our device instance
  Init();

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.on_assoc_req_callback = std::bind(&ConnectTest::SendMultipleResp, this);
  context_.on_auth_req_callback = std::bind(&ConnectTest::SendOpenAuthResp, this);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  // Make sure that the firmware/driver only responded to the first response
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Attempt to associate while a scan is in-progress
TEST_F(ConnectTest, AssocWhileScanning) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.on_assoc_req_callback = std::bind(&ConnectTest::SendMultipleResp, this);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  const uint8_t channels_list[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  wlan_fullmac_scan_req_t scan_req = {
      .txn_id = 42,
      .scan_type = WLAN_SCAN_TYPE_PASSIVE,
      .channels_list = channels_list,
      .channels_count = 11,
      .ssids_list = nullptr,
      .ssids_count = 0,
      .min_channel_time = 0,
      .max_channel_time = 100,
  };
  client_ifc_.if_impl_ops_->start_scan(client_ifc_.if_impl_ctx_, &scan_req);

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
}

TEST_F(ConnectTest, AssocWithWmm) {
  // Create our device instance
  Init();

  uint8_t expected_wmm_param[] = {0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00,
                                  0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00};
  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.expected_wmm_param.insert(context_.expected_wmm_param.end(), expected_wmm_param,
                                     expected_wmm_param + sizeof(expected_wmm_param));
  context_.on_assoc_req_callback = std::bind(&ConnectTest::SendAssocRespWithWmm, this);
  context_.on_auth_req_callback = std::bind(&ConnectTest::SendOpenAuthResp, this);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
}

TEST_F(ConnectTest, AssocStatusAndReasonCodeMismatchHandling) {
  // Create our device instance
  Init();

  ConnectErrorEventInject(BRCMF_E_STATUS_NO_ACK, STATUS_CODE_SUCCESS);
  context_.expected_results.push_back(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(50));
  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that we can successfully associate to a fake AP & disassociate
TEST_F(ConnectTest, DisassocFromSelfTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  start_disassoc_ = true;

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_conf_count, 1U);
}

// Verify that disassoc from fake AP fails when not associated. Also check
// disassoc meant for a different STA, different BSS or when not associated
// is not accepted by the current STA.
TEST_F(ConnectTest, DisassocWithoutConnectTest) {
  // Create our device instance
  Init();
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  // Attempt to disassociate. In this case client is not associated. AP
  // will not transmit the disassoc request
  env_->ScheduleNotification(std::bind(&ConnectTest::StartDisassoc, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::TxFakeDisassocReq, this), zx::msec(50));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 0U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
}

// Verify that disassociate for a different client is ignored
TEST_F(ConnectTest, DisassocNotSelfTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  start_disassoc_ = true;
  disassoc_self_ = false;

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
}

// After association, send disassoc from the AP
TEST_F(ConnectTest, DisassocFromAPTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  disassoc_from_ap_ = true;
  start_disassoc_ = true;

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_ind_count, 1U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);

  EXPECT_EQ(client_ifc_.stats_.disassoc_indications.size(), 1U);
  const wlan_fullmac_disassoc_indication_t& disassoc_ind =
      client_ifc_.stats_.disassoc_indications.front();
  EXPECT_EQ(disassoc_ind.locally_initiated, false);
}

// After assoc & disassoc, send disassoc again to test event handling
TEST_F(ConnectTest, LinkEventTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  disassoc_from_ap_ = true;
  start_disassoc_ = true;

  env_->Run(kTestDuration);

  // Send Deauth frame after disassociation
  DeauthFromAp();
  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_ind_count, 1U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

// After assoc, send a deauth from ap - client should disassociate
TEST_F(ConnectTest, deauth_from_ap) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  deauth_from_ap_ = true;
  start_deauth_ = true;

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.deauth_conf_count, 0U);
  EXPECT_EQ(context_.deauth_ind_count, 1U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
  EXPECT_EQ(context_.disassoc_ind_count, 0U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

// After assoc, send a deauth from client - client should disassociate
TEST_F(ConnectTest, deauth_from_self) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  deauth_from_ap_ = false;
  start_deauth_ = true;

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.deauth_conf_count, 1U);
  EXPECT_EQ(context_.deauth_ind_count, 0U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
  EXPECT_EQ(context_.disassoc_ind_count, 0U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

// Associate, send a deauth from client, associate again, then send deauth from AP.
TEST_F(ConnectTest, deauth_from_self_then_from_ap) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::DeauthClient, this), zx::sec(1));
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::sec(2));
  env_->ScheduleNotification(std::bind(&ConnectTest::DeauthFromAp, this), zx::sec(3));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 2U);
  EXPECT_EQ(context_.deauth_conf_count, 1U);
  EXPECT_EQ(context_.deauth_ind_count, 1U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
  EXPECT_EQ(context_.disassoc_ind_count, 0U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

TEST_F(ConnectTest, simple_reconnect_via_assoc) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::DisassocFromAp, this), zx::sec(2));
  env_->ScheduleNotification(std::bind(&ConnectTest::StartReconnect, this), zx::sec(3));

  env_->Run(kTestDuration);

  EXPECT_EQ(context_.connect_resp_count, 2U);
  EXPECT_EQ(context_.disassoc_ind_count, 1U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

// Reconnect via assoc should succeed since the attempt is made after disassoc completes.
TEST_F(ConnectTest, reconnect_via_assoc_success) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  // Schedule reconnect via assoc when disassoc notification is received at SME.
  start_reconnect_assoc_ = true;

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::DisassocFromAp, this), zx::sec(2));

  env_->Run(kTestDuration);

  // Since reconnect via assoc occurs after disassoc is completed it succeeds.
  EXPECT_EQ(context_.connect_resp_count, 2U);
  EXPECT_EQ(context_.disassoc_ind_count, 1U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

// Reconnect via assoc should fail since the attempt is made soon after SME is notified but before
// disassoc completes.
TEST_F(ConnectTest, reconnect_assoc_fails) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  // Issue reconnect assoc soon after disassoc notification is received at SME.
  start_reconnect_assoc_instant_ = true;

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::DisassocFromAp, this), zx::sec(2));

  env_->Run(kTestDuration);

  // Although we attempted to reconnect assoc, it fails because disconnect is in progress
  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_ind_count, 1U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
}

TEST_F(ConnectTest, deauth_during_reconnect_via_assoc) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_SUCCESS);
  context_.expected_results.push_front(STATUS_CODE_SUCCESS);

  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&ConnectTest::DisassocFromAp, this), zx::sec(2));
  env_->ScheduleNotification(std::bind(&ConnectTest::StartReconnect, this), zx::sec(3));
  // Schedule a deauth immediately, before the above assoc can complete.
  env_->ScheduleNotification(std::bind(&ConnectTest::DeauthClient, this),
                             zx::sec(3) + zx::usec(500));

  env_->Run(kTestDuration);

  // If the deauth is successful, we will not get the second assoc response.
  // If it fails for some reason (e.g. profile->bssid mismatch), then the
  // assoc response count will be 2.
  EXPECT_EQ(context_.connect_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_ind_count, 1U);
  EXPECT_EQ(context_.ind_locally_initiated_count, 0U);
  EXPECT_EQ(context_.deauth_conf_count, 1U);
}

// Verify that association is retried as per the setting
TEST_F(ConnectTest, AssocMaxRetries) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_REFUSED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  zx_status_t status;
  uint32_t max_assoc_retries = 5;
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  status = brcmf_fil_iovar_int_set(ifp, "assoc_retry_max", max_assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  uint32_t assoc_retries;
  status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_EQ(max_assoc_retries, assoc_retries);
  // Should have received as many refusals as the configured # of retries.
  EXPECT_EQ(assoc_responses_.size(), max_assoc_retries + 1);
  EXPECT_EQ(assoc_responses_.front().status,
            wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);

  // The AP should have received 1 deauth, no matter there were how many firmware assoc retries.
  EXPECT_EQ(deauth_frames_.size(), 1U);
  EXPECT_EQ(deauth_frames_.front(), wlan_ieee80211::ReasonCode::STA_LEAVING);
  // Make sure we got our response from the driver
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that association is retried as per the setting
TEST_F(ConnectTest, AssocMaxRetriesWhenTimedOut) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  uint32_t max_assoc_retries = 5;
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_set(ifp, "assoc_retry_max", max_assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  // Should have not received any responses
  EXPECT_EQ(assoc_responses_.size(), 0U);
  // Make sure we got our response from the driver
  EXPECT_EQ(context_.connect_resp_count, 1U);
}

// Verify that association is attempted when retries is set to zero
TEST_F(ConnectTest, AssocNoRetries) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_REFUSED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  zx_status_t status;
  uint32_t max_assoc_retries = 0;
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  status = brcmf_fil_iovar_int_set(ifp, "assoc_retry_max", max_assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  env_->ScheduleNotification(std::bind(&ConnectTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  uint32_t assoc_retries;
  status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &assoc_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  ASSERT_EQ(max_assoc_retries, assoc_retries);

  // We should have gotten a refusal from the fake AP.
  EXPECT_EQ(assoc_responses_.size(), 1U);
  EXPECT_EQ(assoc_responses_.front().status,
            wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);

  // The AP should have received 1 deauth, no matter there were how many firmware assoc retries.
  EXPECT_EQ(deauth_frames_.size(), 1U);
  EXPECT_EQ(deauth_frames_.front(), wlan_ieee80211::ReasonCode::STA_LEAVING);

  // Make sure we got our response from the driver
  EXPECT_EQ(context_.connect_resp_count, 1U);
}
}  // namespace wlan::brcmfmac
