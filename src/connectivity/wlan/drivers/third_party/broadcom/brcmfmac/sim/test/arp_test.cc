// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

constexpr uint16_t kDefaultCh = 149;
constexpr wlan_channel_t kDefaultChannel = {
    .primary = kDefaultCh, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr simulation::WlanTxInfo kDefaultTxInfo = {.channel = kDefaultChannel};

#define OUR_MAC \
  { 0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa }
#define THEIR_MAC \
  { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x02 }
const common::MacAddr kOurMac(OUR_MAC);
const common::MacAddr kTheirMac(THEIR_MAC);

constexpr wlan_ssid_t kDefaultSsid = {.len = 6, .ssid = "Sim_AP"};

// A simple ARP request
const ether_arp kSampleArpReq = {.ea_hdr = {.ar_hrd = htons(ETH_P_802_3),
                                            .ar_pro = htons(ETH_P_IP),
                                            .ar_hln = 6,
                                            .ar_pln = 4,
                                            .ar_op = htons(ARPOP_REQUEST)},
                                 .arp_sha = OUR_MAC,
                                 .arp_spa = {192, 168, 42, 11},
                                 .arp_tha = THEIR_MAC,
                                 .arp_tpa = {100, 101, 102, 103}};

const std::vector<uint8_t> kDummyData = {0, 1, 2, 3, 4};

class ArpTest : public SimTest {
 public:
  static constexpr zx::duration kTestDuration = zx::sec(100);

  ArpTest() = default;
  void Init();
  void CleanupApInterface();

  // Schedule a future call to a member function
  void ScheduleCall(void (ArpTest::*fn)(), zx::duration when);

  // Send a frame directly into the environment
  void Tx(const std::vector<uint8_t>& ethFrame);

  // Interface management
  zx_status_t StartSoftAP();
  zx_status_t StopSoftAP();
  zx_status_t SetMulticastPromisc(bool enable);
  void ScheduleSetMulticastPromisc(bool enable, zx::duration when);

  // Simulation of client associating to a SoftAP interface
  void TxAssocReq();
  void VerifyAssoc();

  // Client association handlers
  void StartAssoc();
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);

  // Frame processing
  void ScheduleTx(const common::MacAddr& dstAddr, const common::MacAddr& srcAddr,
                  const std::vector<uint8_t>& ethFrame, zx::duration when);
  void ScheduleArpFrameTx(zx::duration when, bool expect_rx);
  void ScheduleNonArpFrameTx(zx::duration when);
  void OnDataRecv(const uint8_t* frame, size_t frame_size);
  static std::vector<uint8_t> CreateEthernetFrame(common::MacAddr dstAddr, common::MacAddr srcAddr,
                                                  uint16_t ethType, const uint8_t* ethBody,
                                                  size_t ethBodySize);

 protected:
  simulation::WlanTxInfo tx_info_ = {.channel = kDefaultChannel};
  unsigned int arp_frames_received_ = 0;
  unsigned int non_arp_frames_received_ = 0;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;

  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Interface handlers
  void OnAssocInd(const wlanif_assoc_ind_t* ind);

  std::vector<uint8_t> ethFrame;
  SimInterface sim_ifc_;
  bool assoc_ind_recv_ = false;

  // Have we completed associating our client interface with a fake AP?
  bool assoc_complete_ = false;
};

wlanif_impl_ifc_protocol_ops_t ArpTest::sme_ops_ = {
    // SME operations
    .on_scan_result = [](void* ctx, const wlanif_scan_result_t* result) {},
    .on_scan_end = [](void* ctx, const wlanif_scan_end_t* end) {},
    .join_conf =
        [](void* ctx, const wlanif_join_confirm_t* resp) {
          static_cast<ArpTest*>(ctx)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* ctx, const wlanif_auth_confirm_t* resp) {
          static_cast<ArpTest*>(ctx)->OnAuthConf(resp);
        },
    .auth_ind = [](void* ctx, const wlanif_auth_ind_t* resp) {},
    .deauth_conf = [](void* ctx, const wlanif_deauth_confirm_t* resp) {},
    .deauth_ind = [](void* ctx, const wlanif_deauth_indication_t* ind) {},
    .assoc_conf =
        [](void* ctx, const wlanif_assoc_confirm_t* resp) {
          static_cast<ArpTest*>(ctx)->OnAssocConf(resp);
        },
    .assoc_ind = [](void* ctx,
                    const wlanif_assoc_ind_t* ind) { static_cast<ArpTest*>(ctx)->OnAssocInd(ind); },
    .disassoc_conf = [](void* ctx, const wlanif_disassoc_confirm_t* resp) {},
    .disassoc_ind = [](void* ctx, const wlanif_disassoc_indication_t* ind) {},
    .start_conf =
        [](void* ctx, const wlanif_start_confirm_t* resp) {
          ASSERT_EQ(resp->result_code, WLAN_START_RESULT_SUCCESS);
        },
    .stop_conf =
        [](void* ctx, const wlanif_stop_confirm_t* resp) {
          ASSERT_EQ(resp->result_code, WLAN_STOP_RESULT_SUCCESS);
        },
    .eapol_conf = [](void* ctx, const wlanif_eapol_confirm_t* resp) {},
    .on_channel_switch = [](void* ctx, const wlanif_channel_switch_info_t* ind) {},
    .signal_report = [](void* ctx, const wlanif_signal_report_indication_t* ind) {},
    .eapol_ind = [](void* ctx, const wlanif_eapol_indication_t* ind) {},
    .stats_query_resp = [](void* ctx, const wlanif_stats_query_response_t* resp) {},
    .relay_captured_frame = [](void* ctx, const wlanif_captured_frame_result_t* result) {},
    .data_recv =
        [](void* ctx, const void* data, size_t data_size, uint32_t flags) {
          static_cast<ArpTest*>(ctx)->OnDataRecv(static_cast<const uint8_t*>(data), data_size);
        },
};

void ArpTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void ArpTest::ScheduleCall(void (ArpTest::*fn)(), zx::duration when) {
  auto cb_fn = std::make_unique<std::function<void()>>();
  *cb_fn = std::bind(fn, this);
  env_->ScheduleNotification(std::move(cb_fn), when);
}

// static
std::vector<uint8_t> ArpTest::CreateEthernetFrame(common::MacAddr dstAddr, common::MacAddr srcAddr,
                                                  uint16_t ethType, const uint8_t* ethBody,
                                                  size_t ethBodySize) {
  std::vector<uint8_t> ethFrame;
  ethFrame.resize(14 + ethBodySize);
  memcpy(ethFrame.data(), &dstAddr, sizeof(dstAddr));
  memcpy(ethFrame.data() + common::kMacAddrLen, &srcAddr, sizeof(srcAddr));
  ethFrame.at(common::kMacAddrLen * 2) = ethType >> 8;
  ethFrame.at(common::kMacAddrLen * 2 + 1) = ethType;
  memcpy(ethFrame.data() + 14, ethBody, ethBodySize);

  return ethFrame;
}

zx_status_t ArpTest::StartSoftAP() {
  wlanif_start_req_t start_req = {
      .ssid = {.len = 6, .data = "Sim_AP"},
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .beacon_period = 100,
      .dtim_period = 100,
      .channel = kDefaultCh,
      .rsne_len = 0,
  };
  sim_ifc_.if_impl_ops_->start_req(sim_ifc_.if_impl_ctx_, &start_req);
  return ZX_OK;
}

zx_status_t ArpTest::SetMulticastPromisc(bool enable) {
  wlanif_impl_protocol_ops_t* ops = sim_ifc_.if_impl_ops_;
  void* ctx = sim_ifc_.if_impl_ctx_;
  return ops->set_multicast_promisc(ctx, enable);
}

void ArpTest::ScheduleSetMulticastPromisc(bool enable, zx::duration when) {
  auto fn = std::make_unique<std::function<void()>>();
  *fn = std::bind(&ArpTest::SetMulticastPromisc, this, enable);
  env_->ScheduleNotification(std::move(fn), when);
}

zx_status_t ArpTest::StopSoftAP() {
  wlanif_stop_req_t stop_req{
      .ssid = {.len = 6, .data = "Sim_AP"},
  };
  sim_ifc_.if_impl_ops_->stop_req(sim_ifc_.if_impl_ctx_, &stop_req);
  return ZX_OK;
}

void ArpTest::OnAssocInd(const wlanif_assoc_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kTheirMac.byte, ETH_ALEN), 0);
  assoc_ind_recv_ = true;
}

void ArpTest::TxAssocReq() {
  // Get the mac address of the SoftAP
  const common::MacAddr mac(kTheirMac);
  simulation::SimAssocReqFrame assoc_req_frame(mac, kOurMac, kDefaultSsid);
  env_->Tx(assoc_req_frame, tx_info_, this);
}

void ArpTest::VerifyAssoc() {
  // Verify the event indications were received and
  // the number of clients
  ASSERT_EQ(assoc_ind_recv_, true);
  brcmf_simdev* sim = device_->GetSim();
  uint16_t num_clients = sim->sim_fw->GetNumClients(sim_ifc_.iface_id_);
  ASSERT_EQ(num_clients, 1U);
}

void ArpTest::CleanupApInterface() {
  StopSoftAP();
  ASSERT_EQ(device_->WlanphyImplDestroyIface(sim_ifc_.iface_id_), ZX_OK);
}

void ArpTest::ScheduleTx(const common::MacAddr& dstAddr, const common::MacAddr& srcAddr,
                         const std::vector<uint8_t>& ethFrame, zx::duration when) {
  auto fn = std::make_unique<std::function<void()>>();
  *fn = std::bind(&ArpTest::Tx, this, ethFrame);
  env_->ScheduleNotification(std::move(fn), when);
}

void ArpTest::Tx(const std::vector<uint8_t>& ethFrame) {
  const ethhdr* eth_hdr = reinterpret_cast<const ethhdr*>(ethFrame.data());
  common::MacAddr dst(eth_hdr->h_dest);
  common::MacAddr src(eth_hdr->h_source);
  simulation::SimQosDataFrame dataFrame(true, false, dst, src, common::kBcastMac, 0, ethFrame);
  env_->Tx(dataFrame, kDefaultTxInfo, this);
}

void ArpTest::OnDataRecv(const uint8_t* frame, size_t size) {
  // For this test only, assume anything that is the right size is an ARP frame
  if (size != sizeof(ethhdr) + sizeof(ether_arp)) {
    non_arp_frames_received_++;
    ASSERT_EQ(size, sizeof(ethhdr) + kDummyData.size());
    EXPECT_EQ(memcmp(kDummyData.data(), &frame[sizeof(ethhdr)], kDummyData.size()), 0);
    return;
  }

  arp_frames_received_++;

  const ethhdr* eth_hdr = reinterpret_cast<const ethhdr*>(frame);
  EXPECT_EQ(memcmp(eth_hdr->h_dest, common::kBcastMac.byte, ETH_ALEN), 0);
  EXPECT_EQ(memcmp(eth_hdr->h_source, kTheirMac.byte, ETH_ALEN), 0);
  EXPECT_EQ(ntohs(eth_hdr->h_proto), ETH_P_ARP);

  const ether_arp* arp_hdr = reinterpret_cast<const ether_arp*>(frame + sizeof(ethhdr));
  EXPECT_EQ(memcmp(arp_hdr, &kSampleArpReq, sizeof(ether_arp)), 0);
}

// Schedule the transmission of an ARP frame
void ArpTest::ScheduleArpFrameTx(zx::duration when, bool expect_rx) {
  ether_arp arp_frame = kSampleArpReq;

  // If we don't expect to receive this frame, corrupt it so that it will fail if it does arrive
  if (!expect_rx) {
    arp_frame.arp_tpa[0] = ~arp_frame.arp_tpa[0];
  }

  std::vector<uint8_t> frame_bytes =
      CreateEthernetFrame(common::kBcastMac, kTheirMac, ETH_P_ARP,
                          reinterpret_cast<const uint8_t*>(&arp_frame), sizeof(arp_frame));
  ScheduleTx(kOurMac, kTheirMac, frame_bytes, when);
}

void ArpTest::ScheduleNonArpFrameTx(zx::duration when) {
  std::vector<uint8_t> frame_bytes =
      CreateEthernetFrame(kOurMac, kTheirMac, 0, kDummyData.data(), kDummyData.size());
  ScheduleTx(kOurMac, kTheirMac, frame_bytes, when);
}

void ArpTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, kTheirMac.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = kDefaultSsid.len;
  memcpy(join_req.selected_bss.ssid.data, kDefaultSsid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = kDefaultChannel;
  join_req.selected_bss.bss_type = WLAN_BSS_TYPE_ANY_BSS;
  sim_ifc_.if_impl_ops_->join_req(sim_ifc_.if_impl_ctx_, &join_req);
}

void ArpTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, kTheirMac.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  sim_ifc_.if_impl_ops_->auth_req(sim_ifc_.if_impl_ctx_, &auth_req);
}

void ArpTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  // Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, kTheirMac.byte, ETH_ALEN);
  sim_ifc_.if_impl_ops_->assoc_req(sim_ifc_.if_impl_ctx_, &assoc_req);
}

void ArpTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) { assoc_complete_ = true; }

// Verify that an ARP frame received by an AP interface is not offloaded, even after multicast
// promiscuous mode is enabled.
TEST_F(ArpTest, SoftApArpOffload) {
  Init();
  ASSERT_EQ(SimTest::StartInterface(WLAN_INFO_MAC_ROLE_AP, &sim_ifc_, &sme_protocol_, kOurMac),
            ZX_OK);
  EXPECT_EQ(StartSoftAP(), ZX_OK);

  // Have the test associate with the AP
  ScheduleCall(&ArpTest::TxAssocReq, zx::sec(1));
  ScheduleCall(&ArpTest::VerifyAssoc, zx::sec(2));

  // Send an ARP frame that we expect to be received
  ScheduleArpFrameTx(zx::sec(3), true);
  ScheduleNonArpFrameTx(zx::sec(4));

  ScheduleSetMulticastPromisc(true, zx::sec(5));

  // Send an ARP frame that we expect to be received
  ScheduleArpFrameTx(zx::sec(6), true);
  ScheduleNonArpFrameTx(zx::sec(7));

  // Stop AP and remove interface
  ScheduleCall(&ArpTest::CleanupApInterface, zx::sec(8));

  env_->Run(kTestDuration);

  // Verify that no ARP frames were offloaded
  EXPECT_EQ(arp_frames_received_, 2U);

  // Verify that no non-ARP frames were suppressed
  EXPECT_EQ(non_arp_frames_received_, 2U);
}

// On a client interface, we expect all ARP frames to be offloaded to firmware, regardless of
// the multicast promiscuous setting.
TEST_F(ArpTest, ClientArpOffload) {
  Init();
  ASSERT_EQ(SimTest::StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &sim_ifc_, &sme_protocol_, kOurMac),
            ZX_OK);

  // Start a fake AP
  simulation::FakeAp ap(env_.get(), kTheirMac, kDefaultSsid, kDefaultChannel);

  // Associate with fake AP
  ScheduleCall(&ArpTest::StartAssoc, zx::sec(1));

  // Send an ARP frame that we expect to be offloaded
  ScheduleArpFrameTx(zx::sec(2), false);
  ScheduleNonArpFrameTx(zx::sec(3));

  ScheduleSetMulticastPromisc(true, zx::sec(4));

  // Send an ARP frame that we expect to be offloaded
  ScheduleArpFrameTx(zx::sec(5), false);
  ScheduleNonArpFrameTx(zx::sec(6));

  env_->Run(kTestDuration);

  // Verify that we completed the association process
  EXPECT_EQ(assoc_complete_, true);

  // Verify that all ARP frames were offloaded
  EXPECT_EQ(arp_frames_received_, 0U);

  // Verify that no non-ARP frames were suppressed
  EXPECT_EQ(non_arp_frames_received_, 2U);
}

}  // namespace wlan::brcmfmac
