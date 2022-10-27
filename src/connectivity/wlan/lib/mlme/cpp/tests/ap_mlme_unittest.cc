// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlan/mlme/ap/ap_mlme.h"

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/lib/mlme/cpp/include/wlan/mlme/mac_frame.h"
#include "src/connectivity/wlan/lib/mlme/cpp/include/wlan/mlme/packet.h"
#include "src/connectivity/wlan/lib/mlme/cpp/tests/mock_device.h"
#include "src/connectivity/wlan/lib/mlme/cpp/tests/test_bss.h"
#include "src/connectivity/wlan/lib/mlme/cpp/tests/test_utils.h"

namespace wlan {

namespace {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

constexpr uint8_t kTestPayload[] = "Hello Fuchsia";

struct Context {
  Context(MockDevice* device, ApMlme* ap, const common::MacAddr& client_addr)
      : device(device), ap(ap), client_addr(client_addr) {
    ap->Init();
  }

  void SendClientAuthReqFrame() {
    device->SendWlanPacket(CreateAuthReqFrame(client_addr));
    ap->RunUntilStalled();
  }

  void SendClientDeauthFrame() {
    device->SendWlanPacket(CreateDeauthFrame(client_addr));
    ap->RunUntilStalled();
  }

  void SendClientAssocReqFrame(cpp20::span<const uint8_t> ssid = kSsid, bool rsne = true) {
    device->SendWlanPacket(CreateAssocReqFrame(client_addr, ssid, rsne));
    ap->RunUntilStalled();
  }

  void SendClientDisassocFrame() {
    device->SendWlanPacket(CreateDisassocFrame(client_addr));
    ap->RunUntilStalled();
  }

  void SendNullDataFrame(bool pwr_mgmt) {
    auto frame = CreateNullDataFrame();
    common::MacAddr bssid(kBssid1);
    frame.hdr()->fc.set_from_ds(0);
    frame.hdr()->fc.set_to_ds(1);
    frame.hdr()->fc.set_pwr_mgmt(pwr_mgmt ? 1 : 0);
    frame.hdr()->addr1 = bssid;
    frame.hdr()->addr2 = client_addr;
    frame.hdr()->addr3 = bssid;
    device->SendWlanPacket(frame.Take());
    ap->RunUntilStalled();
  }

  void SendDataFrame(cpp20::span<const uint8_t> payload) {
    auto pkt = CreateDataFrame(payload);
    auto hdr = pkt->mut_field<DataFrameHeader>(0);
    common::MacAddr bssid(kBssid1);
    hdr->fc.set_from_ds(0);
    hdr->fc.set_to_ds(1);
    hdr->addr1 = bssid;
    hdr->addr2 = client_addr;
    hdr->addr3 = bssid;
    device->SendWlanPacket(std::move(pkt));
    ap->RunUntilStalled();
  }

  void SendEthFrame(cpp20::span<const uint8_t> payload) {
    auto pkt = CreateEthFrame(payload);
    auto hdr = pkt->mut_field<EthernetII>(0);
    hdr->src = common::MacAddr(kBssid1);
    hdr->dest = client_addr;
    ap->QueueEthFrameTx(std::move(pkt));
    ap->RunUntilStalled();
  }

  zx::duration TuPeriodsToDuration(size_t periods) { return zx::usec(1024) * periods; }

  void AdvanceTimeInTuPeriods(size_t periods) {
    ap->AdvanceFakeTime(TuPeriodsToDuration(periods).to_nsecs());
  }

  void StartAp(bool protected_ap = true) {
    device->sme_->StartReq(CreateStartRequest(protected_ap));
    ap->RunUntilStalled();
    device->AssertNextMsgFromSmeChannel<wlan_mlme::StartConfirm>();
    device->wlan_queue.clear();
  }

  void AuthenticateClient() {
    SendClientAuthReqFrame();
    device->sme_->AuthenticateResp(
        CreateAuthResponse(client_addr, wlan_mlme::AuthenticateResultCode::SUCCESS));
    ap->RunUntilStalled();
    device->AssertNextMsgFromSmeChannel<wlan_mlme::AuthenticateIndication>();
    device->wlan_queue.clear();
  }

  void AssociateClient(uint16_t aid) {
    SendClientAssocReqFrame();
    device->sme_->AssociateResp(
        CreateAssocResponse(client_addr, wlan_mlme::AssociateResultCode::SUCCESS, aid));
    ap->RunUntilStalled();
    device->AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>();
    device->wlan_queue.clear();
  }

  void AuthenticateAndAssociateClient(uint16_t aid) {
    AuthenticateClient();
    AssociateClient(aid);
  }

  void EstablishRsna() {
    device->sme_->SetControlledPort(
        CreateSetCtrlPortRequest(client_addr, wlan_mlme::ControlledPortState::OPEN));
    ap->RunUntilStalled();
  }

  void AssertAuthInd(MlmeMsg<wlan_mlme::AuthenticateIndication> msg) {
    EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
    EXPECT_EQ(msg.body()->auth_type, wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
  }

  void AssertDeauthInd(MlmeMsg<wlan_mlme::DeauthenticateIndication> msg,
                       wlan_ieee80211::ReasonCode reason_code) {
    EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
    EXPECT_EQ(msg.body()->reason_code, reason_code);
  }

  void AssertAssocInd(MlmeMsg<wlan_mlme::AssociateIndication> msg, bool rsne = true) {
    EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
    EXPECT_EQ(msg.body()->listen_interval, kListenInterval);
    EXPECT_EQ(std::memcmp(msg.body()->ssid->data(), kSsid, msg.body()->ssid->size()), 0);
    if (rsne) {
      EXPECT_EQ(std::memcmp(msg.body()->rsne->data(), kRsne, sizeof(kRsne)), 0);
    } else {
      EXPECT_FALSE(msg.body()->rsne.has_value());
    }
  }

  void AssertDisassocInd(MlmeMsg<wlan_mlme::DisassociateIndication> msg) {
    EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
    EXPECT_EQ(msg.body()->reason_code, wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC);
  }

  void AssertAuthFrame(WlanPacket pkt) {
    auto frame = TypeCheckWlanFrame<MgmtFrameView<Authentication>>(pkt.pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
    EXPECT_EQ(frame.body()->auth_txn_seq_number, 2);
    EXPECT_EQ(static_cast<wlan_ieee80211::StatusCode>(frame.body()->status_code),
              wlan_ieee80211::StatusCode::SUCCESS);
  }

  void AssertAssocFrame(WlanPacket pkt) {
    auto frame = TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(static_cast<wlan_ieee80211::StatusCode>(frame.body()->status_code),
              wlan_ieee80211::StatusCode::SUCCESS);
    EXPECT_EQ(frame.body()->aid, kAid);
  }

  struct DataFrameAssert {
    unsigned char protected_frame = 1;
    unsigned char more_data = 0;
  };

  void AssertDataFrameSentToClient(WlanPacket pkt, cpp20::span<const uint8_t> expected_payload,
                                   DataFrameAssert asserts = {.protected_frame = 1,
                                                              .more_data = 0}) {
    auto frame = TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.pkt.get());
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame.hdr()->fc.more_data(), asserts.more_data);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.hdr()->fc.protected_frame(), asserts.protected_frame);

    auto llc_frame = frame.NextFrame();
    EXPECT_RANGES_EQ(llc_frame.body_data(), expected_payload);
  }

  void AssertEthFrame(cpp20::span<const uint8_t> pkt, cpp20::span<const uint8_t> expected_payload) {
    BufferReader rdr(pkt);
    auto hdr = rdr.Read<EthernetII>();
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(std::memcmp(hdr->src.byte, client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(hdr->dest.byte, kBssid1, 6), 0);
    EXPECT_EQ(hdr->ether_type_be, 42);
    auto payload = rdr.ReadRemaining();
    EXPECT_RANGES_EQ(payload, expected_payload);
  }

  MockDevice* device;
  ApMlme* ap;
  common::MacAddr client_addr;
};

struct ApInfraBssTest : public ::testing::Test {
  ApInfraBssTest()
      : device(common::MacAddr(kBssid1)),
        ap(&device, true),
        ctx(&device, &ap, common::MacAddr(kClientAddress)) {}

  void TearDown() override { device.sme_->StopReq(CreateStopRequest()); }

  MockDevice device;
  ApMlme ap;
  Context ctx;
};

TEST_F(ApInfraBssTest, StartAp) {
  device.sme_->StartReq(CreateStartRequest(true));
  ap.RunUntilStalled();
  ASSERT_EQ(device.AssertNextMsgFromSmeChannel<wlan_mlme::StartConfirm>().body()->result_code,
            wlan_mlme::StartResultCode::SUCCESS);
}

TEST_F(ApInfraBssTest, ProbeRequest_Success) {
  ctx.StartAp();

  // Send probe request frame
  ctx.device->SendWlanPacket(CreateProbeRequest());
  ctx.ap->RunUntilStalled();

  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  auto frame = TypeCheckWlanFrame<MgmtFrameView<ProbeResponse>>(pkt.pkt.get());
  EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
  EXPECT_EQ(frame.body()->timestamp, 0u);
  EXPECT_EQ(frame.body()->capability_info.val(), CapabilityInfo().val());
  EXPECT_EQ(frame.body()->beacon_interval, 100);
}

TEST_F(ApInfraBssTest, Authenticate_Success) {
  ctx.StartAp();

  // Send authentication request frame
  ctx.SendClientAuthReqFrame();

  // Verify that an Authentication.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());
  ctx.AssertAuthInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AuthenticateIndication>());

  // Simulate SME sending MLME-AUTHENTICATE.response msg with a success code
  ctx.device->sme_->AuthenticateResp(
      CreateAuthResponse(ctx.client_addr, wlan_mlme::AuthenticateResultCode::SUCCESS));
  ctx.ap->RunUntilStalled();

  // Verify authentication response frame for the client
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  ctx.AssertAuthFrame(std::move(*device.wlan_queue.begin()));
}

TEST_F(ApInfraBssTest, Authenticate_SmeRefuses) {
  ctx.StartAp();

  // Send authentication request frame
  ctx.SendClientAuthReqFrame();

  // Simulate SME sending MLME-AUTHENTICATE.response msg with a refusal code
  ctx.device->sme_->AuthenticateResp(
      CreateAuthResponse(ctx.client_addr, wlan_mlme::AuthenticateResultCode::REFUSED));
  ctx.ap->RunUntilStalled();

  // Verify that authentication response frame for client is a refusal
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  auto frame = TypeCheckWlanFrame<MgmtFrameView<Authentication>>(pkt.pkt.get());
  EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
  EXPECT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
  EXPECT_EQ(frame.body()->auth_txn_seq_number, 2);
  EXPECT_EQ(static_cast<wlan_ieee80211::StatusCode>(frame.body()->status_code),
            wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
}

TEST_F(ApInfraBssTest, Authenticate_Timeout) {
  ctx.StartAp();

  // Send authentication request frame
  ctx.SendClientAuthReqFrame();

  // No timeout yet, so nothing happens. Even if another auth request comes,
  // it's a no-op
  ctx.AdvanceTimeInTuPeriods(59000);
  ctx.SendClientAuthReqFrame();
  EXPECT_TRUE(device.wlan_queue.empty());

  // Timeout triggers. Verify that if another auth request comes, it's
  // processed.
  ctx.AdvanceTimeInTuPeriods(1000);
  ctx.SendClientAuthReqFrame();
  EXPECT_TRUE(device.wlan_queue.empty());
  ctx.AssertAuthInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AuthenticateIndication>());
}

TEST_F(ApInfraBssTest, ReauthenticateWhileAuthenticated) {
  ctx.StartAp();
  ctx.AuthenticateClient();

  // Send authentication request frame
  ctx.SendClientAuthReqFrame();

  // Verify that an Authentication.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());

  // Simulate SME sending MLME-AUTHENTICATE.response msg with a success code
  ctx.device->sme_->AuthenticateResp(
      CreateAuthResponse(ctx.client_addr, wlan_mlme::AuthenticateResultCode::SUCCESS));
  ctx.ap->RunUntilStalled();

  // Verify authentication response frame for the client
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  ctx.AssertAuthFrame(std::move(*device.wlan_queue.begin()));
}

TEST_F(ApInfraBssTest, DeauthenticateWhileAuthenticated) {
  ctx.StartAp();
  ctx.AuthenticateClient();

  // Send deauthentication frame
  ctx.SendClientDeauthFrame();

  ctx.AssertDeauthInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::DeauthenticateIndication>(),
                      wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DEAUTH);

  // Expect association context is still blank.
  wlan_assoc_ctx_t expected_ctx = {};
  std::memset(&expected_ctx, 0, sizeof(expected_ctx));
  const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
  EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Associate_Success) {
  ctx.StartAp();
  ctx.AuthenticateClient();

  // Send association request frame
  ctx.SendClientAssocReqFrame();

  // Verify that an Association.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());
  ctx.AssertAssocInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>());

  // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
  ctx.device->sme_->AssociateResp(
      CreateAssocResponse(ctx.client_addr, wlan_mlme::AssociateResultCode::SUCCESS, kAid));
  ctx.ap->RunUntilStalled();

  // Verify association response frame for the client
  // WLAN queue should have AssociateResponse
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  ctx.AssertAssocFrame(std::move(*device.wlan_queue.begin()));

  ctx.EstablishRsna();

  device.wlan_queue.clear();
  ctx.SendEthFrame(kTestPayload);
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
}

TEST_F(ApInfraBssTest, Associate_AssociationContext) {
  ctx.StartAp();
  ctx.AuthenticateClient();

  // Send association request frame
  ctx.SendClientAssocReqFrame();

  // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
  ctx.device->sme_->AssociateResp(
      CreateAssocResponse(ctx.client_addr, wlan_mlme::AssociateResultCode::SUCCESS, kAid));
  ctx.ap->RunUntilStalled();

  // Expect association context has been set properly.
  const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
  EXPECT_EQ(std::memcmp(actual_ctx->bssid, ctx.client_addr.byte, 6), 0);
  EXPECT_EQ(actual_ctx->aid, kAid);
  std::vector<uint8_t> rates{0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12,
                             0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
  EXPECT_EQ(actual_ctx->rates_cnt, rates.size());
  for (size_t i = 0; i < rates.size(); i++) {
    EXPECT_EQ(actual_ctx->rates[i], rates[i]);
  }
  EXPECT_FALSE(actual_ctx->has_ht_cap);
}

TEST_F(ApInfraBssTest, Associate_MultipleClients) {
  ctx.StartAp();
  Context client2_ctx(&device, &ap, common::MacAddr({0x22, 0x22, 0x22, 0x22, 0x22, 0x22}));

  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Eth frame from client 2 is no-op since client 2 is not associated
  client2_ctx.SendEthFrame(kTestPayload);
  ASSERT_TRUE(device.wlan_queue.empty());

  uint16_t client2_aid = 5;
  client2_ctx.AuthenticateAndAssociateClient(client2_aid);
  client2_ctx.EstablishRsna();

  // Test sending message to client 1
  ctx.SendEthFrame(kTestPayload);
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload);
  device.wlan_queue.clear();

  // Test sending message to client 2
  client2_ctx.SendEthFrame(kTestPayload);
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  pkt = std::move(*device.wlan_queue.begin());
  client2_ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload);
}

TEST_F(ApInfraBssTest, Associate_SmeRefuses) {
  ctx.StartAp();
  ctx.AuthenticateClient();

  // Send association request frame
  ctx.SendClientAssocReqFrame();

  // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
  ctx.device->sme_->AssociateResp(CreateAssocResponse(
      ctx.client_addr, wlan_mlme::AssociateResultCode::REFUSED_CAPABILITIES_MISMATCH, 0));
  ctx.ap->RunUntilStalled();

  // Expect association context has not been set (blank).
  wlan_assoc_ctx_t expected_ctx = {};
  std::memset(&expected_ctx, 0, sizeof(expected_ctx));
  const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
  EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);

  // Verify association response frame for the client is a refusal
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  auto frame = TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.pkt.get());
  EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
  EXPECT_EQ(static_cast<wlan_ieee80211::StatusCode>(frame.body()->status_code),
            wlan_ieee80211::StatusCode::REFUSED_CAPABILITIES_MISMATCH);
  EXPECT_EQ(frame.body()->aid, 0);

  device.wlan_queue.clear();
  // Sending frame should be a no-op since association fails
  ctx.SendEthFrame(kTestPayload);
  EXPECT_TRUE(device.wlan_queue.empty());
}

TEST_F(ApInfraBssTest, Associate_Timeout) {
  ctx.StartAp();
  ctx.AuthenticateClient();

  // Send association request frame
  ctx.SendClientAssocReqFrame();
  ctx.device->AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>();

  // No timeout yet, so nothing happens. Even if another assoc request comes,
  // it's a no-op
  ctx.AdvanceTimeInTuPeriods(59000);
  ctx.SendClientAssocReqFrame();
  EXPECT_TRUE(device.wlan_queue.empty());

  // Timeout triggers. Verify that if another assoc request comes, it's
  // processed.
  ctx.AdvanceTimeInTuPeriods(1000);
  ctx.SendClientAssocReqFrame();
  EXPECT_TRUE(device.wlan_queue.empty());
  ctx.AssertAssocInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>());

  // Expect association context has been cleared.
  wlan_assoc_ctx_t expected_ctx = {};
  std::memset(&expected_ctx, 0, sizeof(expected_ctx));
  const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
  EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Associate_EmptySsid) {
  ctx.StartAp(false);
  ctx.AuthenticateClient();

  // Send association request frame without an SSID
  auto ssid = cpp20::span<uint8_t>();
  ctx.SendClientAssocReqFrame(ssid, true);
  ctx.device->AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>();

  // Verify that no Association.indication msg is sent out
  ASSERT_TRUE(device.wlan_queue.empty());

  // Send a valid association request frame
  ctx.SendClientAssocReqFrame();

  // Verify that an Association.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());
  ctx.AssertAssocInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>());
}

TEST_F(ApInfraBssTest, Associate_EmptyRsne) {
  ctx.StartAp(false);
  ctx.AuthenticateClient();

  // Send association request frame
  ctx.SendClientAssocReqFrame(kSsid, false);

  // Verify that an Association.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());
  ctx.AssertAssocInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>(), false);
}

TEST_F(ApInfraBssTest, ReauthenticateWhileAssociated) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send authentication request frame
  ctx.SendClientAuthReqFrame();

  // Verify that an Authentication.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());
  ctx.AssertAuthInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AuthenticateIndication>());

  // Simulate SME sending MLME-AUTHENTICATE.response msg with a success code
  ctx.device->sme_->AuthenticateResp(
      CreateAuthResponse(ctx.client_addr, wlan_mlme::AuthenticateResultCode::SUCCESS));
  ctx.ap->RunUntilStalled();

  // Verify authentication response frame for the client
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  ctx.AssertAuthFrame(std::move(*device.wlan_queue.begin()));
}

TEST_F(ApInfraBssTest, AuthenticateAndAssociateWhileAssociated) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Reauthenticate
  ctx.AuthenticateClient();

  // Send association request frame
  ctx.SendClientAssocReqFrame();

  // Verify that an Association.indication msg is sent out (to SME)
  ASSERT_TRUE(device.wlan_queue.empty());
  ctx.AssertAssocInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::AssociateIndication>());

  // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
  ctx.device->sme_->AssociateResp(
      CreateAssocResponse(ctx.client_addr, wlan_mlme::AssociateResultCode::SUCCESS, kAid));
  ctx.ap->RunUntilStalled();

  // Verify association response frame for the client
  // WLAN queue should have AssociateResponse
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  ctx.AssertAssocFrame(std::move(*device.wlan_queue.begin()));

  ctx.EstablishRsna();

  device.wlan_queue.clear();
  ctx.SendEthFrame(kTestPayload);
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
}

TEST_F(ApInfraBssTest, DeauthenticateWhileAssociated) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send deauthentication frame
  ctx.SendClientDeauthFrame();
  ctx.AssertDeauthInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::DeauthenticateIndication>(),
                      wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DEAUTH);

  // Expect association context has been cleared.
  wlan_assoc_ctx_t expected_ctx = {};
  std::memset(&expected_ctx, 0, sizeof(expected_ctx));
  const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
  EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Disassociate) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send deauthentication frame
  ctx.SendClientDisassocFrame();
  ctx.AssertDisassocInd(device.AssertNextMsgFromSmeChannel<wlan_mlme::DisassociateIndication>());

  // Expect association context has been cleared.
  wlan_assoc_ctx_t expected_ctx = {};
  std::memset(&expected_ctx, 0, sizeof(expected_ctx));
  const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
  EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Exchange_Eapol_Frames) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send MLME-EAPOL.request.
  ctx.device->sme_->EapolReq(CreateEapolRequest(common::MacAddr(kBssid1), ctx.client_addr));
  ctx.ap->RunUntilStalled();

  // Verify EAPOL frame was sent.
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  auto frame = TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.pkt.get());
  EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
  EXPECT_EQ(frame.body()->protocol_id_be, htobe16(kEapolProtocolId));
  auto type_checked_frame = frame.SkipHeader().CheckBodyType<EapolHdr>();
  ASSERT_TRUE(type_checked_frame);
  auto llc_eapol_frame = type_checked_frame.CheckLength();
  ASSERT_TRUE(llc_eapol_frame);
  EXPECT_EQ(llc_eapol_frame.body_len(), static_cast<size_t>(5));
  EXPECT_RANGES_EQ(llc_eapol_frame.body_data(), kEapolPdu);
  EXPECT_EQ(pkt.tx_info.tx_flags, WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY);
}

TEST_F(ApInfraBssTest, SendFrameAfterAssociation) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Have BSS process Eth frame.
  ctx.SendEthFrame(kTestPayload);

  // Verify a data WLAN frame was sent.
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload);
}

TEST_F(ApInfraBssTest, UnprotectedApReceiveFramesAfterAssociation) {
  ctx.StartAp(false);

  // Simulate unauthenticated client sending data frames, which should emit a deauth to MLME, deauth
  // to the client, and no eth frame
  ctx.SendDataFrame(kTestPayload);
  ctx.device->AssertNextMsgFromSmeChannel<wlan_mlme::DeauthenticateIndication>();
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  device.wlan_queue.clear();
  ASSERT_TRUE(device.eth_queue.empty());

  ctx.AuthenticateClient();

  // Simulate unassociated client sending data frames, which should emit a disassoc to MLME,
  // disassoc to the client, and no eth frame
  ctx.SendDataFrame(kTestPayload);
  ctx.device->AssertNextMsgFromSmeChannel<wlan_mlme::DisassociateIndication>();
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  device.wlan_queue.clear();
  ASSERT_TRUE(device.wlan_queue.empty());

  ctx.AssociateClient(kAid);
  ctx.SendDataFrame(kTestPayload);
  ASSERT_TRUE(device.wlan_queue.empty());

  // Verify ethernet frame is sent out and is correct
  auto eth_frames = device.GetEthPackets();
  ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
  ctx.AssertEthFrame(std::move(*eth_frames.begin()), kTestPayload);
}

TEST_F(ApInfraBssTest, MlmeDeauthReqWhileAssociated) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send MLME-DEAUTHENTICATE.request
  const auto reason_code = wlan_ieee80211::ReasonCode::FOURWAY_HANDSHAKE_TIMEOUT;
  ctx.device->sme_->DeauthenticateReq(CreateDeauthRequest(ctx.client_addr, reason_code));
  ctx.ap->RunUntilStalled();

  // Verify deauthenticate frame was sent
  ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  auto frame = TypeCheckWlanFrame<MgmtFrameView<Deauthentication>>(pkt.pkt.get());
  EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
  EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
  EXPECT_EQ(frame.body()->reason_code, static_cast<uint16_t>(reason_code));
}

TEST_F(ApInfraBssTest, SetKeys) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send MLME-SETKEYS.request
  auto key_data = std::vector(std::cbegin(kKeyData), std::cend(kKeyData));
  ctx.device->sme_->SetKeysReq(
      CreateSetKeysRequest(ctx.client_addr, key_data, wlan_mlme::KeyType::PAIRWISE));
  ctx.ap->RunUntilStalled();

  ASSERT_EQ(device.GetKeys().size(), static_cast<size_t>(1));
  auto key_config = std::move(device.GetKeys()[0]);
  EXPECT_EQ(std::memcmp(key_config.key, kKeyData, sizeof(kKeyData)), 0);
  EXPECT_EQ(key_config.key_idx, 1);
  EXPECT_EQ(key_config.key_type, WLAN_KEY_TYPE_PAIRWISE);
  EXPECT_EQ(std::memcmp(key_config.peer_addr, ctx.client_addr.byte, sizeof(ctx.client_addr)), 0);
  EXPECT_EQ(std::memcmp(key_config.cipher_oui, kCipherOui, sizeof(kCipherOui)), 0);
  EXPECT_EQ(key_config.cipher_type, kCipherSuiteType);
}

TEST_F(ApInfraBssTest, SetKeys_IgnoredForUnprotectedAp) {
  ctx.StartAp(false);
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Send MLME-SETKEYS.request
  auto key_data = std::vector(std::cbegin(kKeyData), std::cend(kKeyData));
  ctx.device->sme_->SetKeysReq(
      CreateSetKeysRequest(ctx.client_addr, key_data, wlan_mlme::KeyType::PAIRWISE));
  ctx.ap->RunUntilStalled();

  EXPECT_TRUE(device.GetKeys().empty());
}

TEST_F(ApInfraBssTest, PowerSaving_AfterControlledPortOpens) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Simulate client sending null data frame with power saving.
  auto pwr_mgmt = true;
  ctx.SendNullDataFrame(pwr_mgmt);
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

  // Two Ethernet frames arrive. Verify no WLAN frame is sent out yet.
  std::vector<uint8_t> payload2 = {'m', 's', 'g', '2'};
  ctx.SendEthFrame(kTestPayload);
  ctx.SendEthFrame(payload2);
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

  // Client notifies that it wakes up. Buffered frames should be sent out now
  ctx.SendNullDataFrame(!pwr_mgmt);
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(2));
  auto pkt = std::move(*device.wlan_queue.begin());
  ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload,
                                  {
                                      .protected_frame = 1,
                                      .more_data = 1,
                                  });
  pkt = std::move(*(device.wlan_queue.begin() + 1));
  ctx.AssertDataFrameSentToClient(std::move(pkt), payload2, {.protected_frame = 1});
}

TEST_F(ApInfraBssTest, PowerSaving_UnprotectedAp) {
  // For unprotected AP, power saving should work as soon as client is
  // associated
  ctx.StartAp(false);
  ctx.AuthenticateAndAssociateClient(kAid);

  // Simulate client sending null data frame with power saving.
  auto pwr_mgmt = true;
  ctx.SendNullDataFrame(pwr_mgmt);
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

  // Two Ethernet frames arrive. Verify no WLAN frame is sent out yet.
  std::vector<uint8_t> payload2 = {'m', 's', 'g', '2'};
  ctx.SendEthFrame(kTestPayload);
  ctx.SendEthFrame(payload2);
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

  // Client notifies that it wakes up. Buffered frames should be sent out now
  ctx.SendNullDataFrame(!pwr_mgmt);
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(2));
  auto pkt = std::move(*device.wlan_queue.begin());
  ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload,
                                  {
                                      .protected_frame = 0,
                                      .more_data = 1,
                                  });
  pkt = std::move(*(device.wlan_queue.begin() + 1));
  ctx.AssertDataFrameSentToClient(std::move(pkt), payload2,
                                  {
                                      .protected_frame = 0,
                                      .more_data = 0,
                                  });
}

TEST_F(ApInfraBssTest, OutboundFramesAreProtectedAfterControlledPortOpens) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Have BSS process Eth frame.
  ctx.SendEthFrame(kTestPayload);

  // Verify a data WLAN frame was sent with protected frame flag set
  EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
  auto pkt = std::move(*device.wlan_queue.begin());
  ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload, {.protected_frame = 1});
}

TEST_F(ApInfraBssTest, ReceiveFrames_BeforeControlledPortOpens) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);

  // Simulate client sending data frame to AP
  ASSERT_TRUE(device.eth_queue.empty());
  ctx.SendDataFrame(kTestPayload);

  // For protected AP, controlled port is not opened until RSNA is established,
  // so data frame should be ignored
  EXPECT_TRUE(device.eth_queue.empty());

  auto key_data = std::vector(std::cbegin(kKeyData), std::cend(kKeyData));
  ctx.device->sme_->SetKeysReq(
      CreateSetKeysRequest(ctx.client_addr, key_data, wlan_mlme::KeyType::PAIRWISE));
  ctx.ap->RunUntilStalled();

  // Simulate client sending data frame to AP
  ASSERT_TRUE(device.eth_queue.empty());
  ctx.SendDataFrame(kTestPayload);

  // Setting keys doesn't implicit open the controlled port, hence data frame is
  // still ignored
  EXPECT_TRUE(device.eth_queue.empty());
}

TEST_F(ApInfraBssTest, ReceiveFrames_AfterControlledPortOpens) {
  ctx.StartAp();
  ctx.AuthenticateAndAssociateClient(kAid);
  ctx.EstablishRsna();

  // Simulate client sending data frame to AP
  ASSERT_TRUE(device.eth_queue.empty());
  ctx.SendDataFrame(kTestPayload);

  // Verify ethernet frame is sent out and is correct
  auto eth_frames = device.GetEthPackets();
  ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
  ctx.AssertEthFrame(std::move(*eth_frames.begin()), kTestPayload);
}

}  // namespace
}  // namespace wlan
