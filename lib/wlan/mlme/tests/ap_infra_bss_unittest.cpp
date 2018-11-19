// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/infra_bss.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include "mock_device.h"
#include "test_bss.h"
#include "test_utils.h"

#include <gtest/gtest.h>

namespace wlan {

namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

const std::vector<uint8_t> kTestPayload = {'F', 'u', 'c', 'h', 's', 'i', 'a'};

struct Context {
    Context(MockDevice* device, InfraBss* bss, common::MacAddr client_addr)
        : device(device), bss(bss), client_addr(client_addr) {}

    zx_status_t HandleFrame(std::function<zx_status_t(fbl::unique_ptr<Packet>*)> create_frame) {
        fbl::unique_ptr<Packet> pkt;
        auto status = create_frame(&pkt);
        if (status != ZX_OK) { return status; }
        bss->HandleAnyFrame(std::move(pkt));
        return ZX_OK;
    }

    template <typename M>
    zx_status_t HandleMlmeMsg(std::function<zx_status_t(MlmeMsg<M>*)> create_msg) {
        MlmeMsg<M> msg;
        auto status = create_msg(&msg);
        if (status != ZX_OK) { return status; }
        bss->HandleMlmeMsg(msg);
        return ZX_OK;
    }

    void HandleTimeout() { bss->HandleTimeout(client_addr); }

    template <typename FV> FV TypeCheckWlanFrame(Packet* pkt) {
        EXPECT_EQ(pkt->peer(), Packet::Peer::kWlan);
        auto type_checked_frame = FV::CheckType(pkt);
        EXPECT_TRUE(type_checked_frame);
        auto frame = type_checked_frame.CheckLength();
        EXPECT_TRUE(frame);
        return frame;
    }

    zx_status_t SendStartReqMsg(bool protected_ap) {
        MlmeMsg<wlan_mlme::StartRequest> start_req;
        auto status = CreateStartRequest(&start_req, protected_ap);
        if (status != ZX_OK) { return status; }
        bss->Start(std::move(start_req));
        return ZX_OK;
    }

    zx_status_t SendClientAuthReqFrame() {
        return HandleFrame([=](auto pkt) { return CreateAuthReqFrame(pkt, client_addr); });
    }

    zx_status_t SendClientDeauthFrame() {
        return HandleFrame([=](auto pkt) { return CreateDeauthFrame(pkt, client_addr); });
    }

    zx_status_t SendClientAssocReqFrame(Span<const uint8_t> ssid = kSsid, bool rsn = true) {
        return HandleFrame(
            [=](auto pkt) { return CreateAssocReqFrame(pkt, client_addr, ssid, rsn); });
    }

    zx_status_t SendClientDisassocFrame() {
        return HandleFrame([=](auto pkt) { return CreateDisassocFrame(pkt, client_addr); });
    }

    zx_status_t SendNullDataFrame(bool pwr_mgmt) {
        return HandleFrame([=](auto pkt) {
            auto frame = CreateNullDataFrame();
            if (frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }

            common::MacAddr bssid(kBssid1);
            frame.hdr()->fc.set_from_ds(0);
            frame.hdr()->fc.set_to_ds(1);
            frame.hdr()->fc.set_pwr_mgmt(pwr_mgmt ? 1 : 0);
            frame.hdr()->addr1 = bssid;
            frame.hdr()->addr2 = client_addr;
            frame.hdr()->addr3 = bssid;
            *pkt = frame.Take();
            return ZX_OK;
        });
    }

    zx_status_t SendDataFrame() {
        return HandleFrame([=](auto pkt) {
            auto frame = CreateDataFrame(kTestPayload.data(), kTestPayload.size());
            if (frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }

            common::MacAddr bssid(kBssid1);
            frame.hdr()->fc.set_from_ds(0);
            frame.hdr()->fc.set_to_ds(1);
            frame.hdr()->addr1 = bssid;
            frame.hdr()->addr2 = client_addr;
            frame.hdr()->addr3 = bssid;
            *pkt = frame.Take();
            return ZX_OK;
        });
    }

    zx_status_t SendEthFrame(std::vector<uint8_t> payload) {
        return HandleFrame([&](auto pkt) {
            auto eth_frame = CreateEthFrame(payload.data(), payload.size());
            if (eth_frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }
            eth_frame.hdr()->src = common::MacAddr(kBssid1);
            eth_frame.hdr()->dest = client_addr;
            *pkt = eth_frame.Take();
            return ZX_OK;
        });
    }

    zx_status_t SendDeauthReqMsg(wlan_mlme::ReasonCode reason_code) {
        return HandleMlmeMsg<wlan_mlme::DeauthenticateRequest>([=](auto msg) {
            return CreateDeauthRequest(msg, common::MacAddr(kClientAddress), reason_code);
        });
    }

    zx_status_t SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes result_code) {
        return HandleMlmeMsg<wlan_mlme::AuthenticateResponse>(
            [=](auto msg) { return CreateAuthResponse(msg, client_addr, result_code); });
    }

    zx_status_t SendAssocResponseMsg(wlan_mlme::AssociateResultCodes result_code, uint16_t aid) {
        return HandleMlmeMsg<wlan_mlme::AssociateResponse>(
            [=](auto msg) { return CreateAssocResponse(msg, client_addr, result_code, aid); });
    }

    zx_status_t SendEapolRequestMsg() {
        return HandleMlmeMsg<wlan_mlme::EapolRequest>(
            [=](auto msg) { return CreateEapolRequest(msg, client_addr); });
    }

    zx_status_t SendSetKeysRequestMsg() {
        return HandleMlmeMsg<wlan_mlme::SetKeysRequest>([=](auto msg) {
            auto key_data = std::vector(kKeyData, kKeyData + sizeof(kKeyData));
            return CreateSetKeysRequest(msg, client_addr, key_data, wlan_mlme::KeyType::PAIRWISE);
        });
    }

    zx::duration TuPeriodsToDuration(size_t periods) { return zx::usec(1024) * periods; }

    void SetTimeInTuPeriods(size_t periods) {
        device->SetTime(zx::time(0) + TuPeriodsToDuration(periods));
    }

    void StartAp(bool protected_ap = true) {
        SendStartReqMsg(protected_ap);
        device->svc_queue.clear();
        device->wlan_queue.clear();
    }

    void AuthenticateClient() {
        SendClientAuthReqFrame();
        SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes::SUCCESS);
        device->svc_queue.clear();
        device->wlan_queue.clear();
    }

    void AssociateClient(uint16_t aid) {
        SendClientAssocReqFrame();
        SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::SUCCESS, aid);
        device->svc_queue.clear();
        device->wlan_queue.clear();
    }

    void AuthenticateAndAssociateClient(uint16_t aid) {
        AuthenticateClient();
        AssociateClient(aid);
    }

    void EstablishRsna() {
        // current implementation naively assumes that RSNA is established as soon as one key is set
        SendSetKeysRequestMsg();
    }

    void AssertAuthInd(fbl::unique_ptr<Packet> pkt) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::AuthenticateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::AuthenticateIndication>::FromPacket(std::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
        EXPECT_EQ(msg.body()->auth_type, wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
    }

    void AssertDeauthInd(fbl::unique_ptr<Packet> pkt, wlan_mlme::ReasonCode reason_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::DeauthenticateIndication> msg;
        auto status =
            MlmeMsg<wlan_mlme::DeauthenticateIndication>::FromPacket(std::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
        EXPECT_EQ(msg.body()->reason_code, reason_code);
    }

    void AssertAssocInd(fbl::unique_ptr<Packet> pkt, bool rsn = true) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::AssociateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::AssociateIndication>::FromPacket(std::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
        EXPECT_EQ(msg.body()->listen_interval, kListenInterval);
        EXPECT_EQ(std::memcmp(msg.body()->ssid->data(), kSsid, msg.body()->ssid->size()), 0);
        if (rsn) {
            EXPECT_EQ(std::memcmp(msg.body()->rsn->data(), kRsne, sizeof(kRsne)), 0);
        } else {
            EXPECT_TRUE(msg.body()->rsn.is_null());
        }
    }

    void AssertDisassocInd(fbl::unique_ptr<Packet> pkt) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::DisassociateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::DisassociateIndication>::FromPacket(std::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), client_addr.byte, 6), 0);
        EXPECT_EQ(msg.body()->reason_code,
                  static_cast<uint16_t>(wlan_mlme::ReasonCode::LEAVING_NETWORK_DISASSOC));
    }

    struct DataFrameAssert {
        unsigned char protected_frame = 0;
        unsigned char more_data = 0;
    };

    void AssertDataFrameSentToClient(fbl::unique_ptr<Packet> pkt,
                                     std::vector<uint8_t> expected_payload,
                                     DataFrameAssert asserts = {.protected_frame = 0,
                                                                .more_data = 0}) {
        auto frame = TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.get());
        ASSERT_TRUE(frame);
        EXPECT_EQ(frame.hdr()->fc.more_data(), asserts.more_data);
        EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, client_addr.byte, 6), 0);
        EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
        EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
        EXPECT_EQ(frame.hdr()->fc.protected_frame(), asserts.protected_frame);

        auto llc_hdr = frame.body();
        EXPECT_EQ(frame.body_len() - llc_hdr->len(), expected_payload.size());
        EXPECT_EQ(std::memcmp(llc_hdr->payload, expected_payload.data(), expected_payload.size()),
                  0);
    }

    void AssertEthFrame(fbl::unique_ptr<Packet> pkt, std::vector<uint8_t> expected_payload) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kEthernet);
        EthFrameView frame(pkt.get());
        EXPECT_EQ(std::memcmp(frame.hdr()->src.byte, client_addr.byte, 6), 0);
        EXPECT_EQ(std::memcmp(frame.hdr()->dest.byte, kBssid1, 6), 0);
        EXPECT_EQ(frame.hdr()->ether_type, 42);
        EXPECT_EQ(frame.body_len(), expected_payload.size());
        EXPECT_EQ(std::memcmp(frame.body()->data, expected_payload.data(), expected_payload.size()),
                  0);
    }

    MockDevice* device;
    InfraBss* bss;
    common::MacAddr client_addr;
};

struct ApInfraBssTest : public ::testing::Test {
    ApInfraBssTest()
        : bss(&device, fbl::make_unique<BeaconSender>(&device), common::MacAddr(kBssid1)),
          ctx(&device, &bss, common::MacAddr(kClientAddress)) {}

    void SetUp() override { device.SetTime(zx::time(0)); }
    void TearDown() override { bss.Stop(); }

    MockDevice device;
    InfraBss bss;
    Context ctx;
};

TEST_F(ApInfraBssTest, StartAp) {
    ctx.StartAp();
    ASSERT_TRUE(bss.IsStarted());
}

TEST_F(ApInfraBssTest, Authenticate_Success) {
    ctx.StartAp();

    // Send authentication request frame
    ctx.SendClientAuthReqFrame();

    // Verify that an Authentication.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto auth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateIndOrdinal>);
    ASSERT_FALSE(auth_inds.empty());
    ctx.AssertAuthInd(std::move(*auth_inds.begin()));

    // Simulate SME sending MLME-AUTHENTICATE.response msg with a success code
    ctx.SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes::SUCCESS);
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    fbl::unique_ptr<Packet> pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<MgmtFrameView<Authentication>>(pkt.get());

    // Verify authentication response frame for the client
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
    EXPECT_EQ(frame.body()->auth_txn_seq_number, 2);
    EXPECT_EQ(frame.body()->status_code, status_code::kSuccess);
}

TEST_F(ApInfraBssTest, Authenticate_SmeRefuses) {
    ctx.StartAp();

    // Send authentication request frame
    ctx.SendClientAuthReqFrame();

    // Simulate SME sending MLME-AUTHENTICATE.response msg with a refusal code
    ctx.SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes::REFUSED);

    // Verify that authentication response frame for client is a refusal
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<MgmtFrameView<Authentication>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
    EXPECT_EQ(frame.body()->auth_txn_seq_number, 2);
    EXPECT_EQ(frame.body()->status_code, status_code::kRefused);
}

TEST_F(ApInfraBssTest, Authenticate_Timeout) {
    ctx.StartAp();

    // Send authentication request frame
    ctx.SendClientAuthReqFrame();
    device.svc_queue.clear();

    // No timeout yet, so nothing happens. Even if another auth request comes, it's a no-op
    ctx.SetTimeInTuPeriods(59000);
    ctx.HandleTimeout();
    ctx.SendClientAuthReqFrame();
    EXPECT_TRUE(device.svc_queue.empty());
    EXPECT_TRUE(device.wlan_queue.empty());

    // Timeout triggers. Verify that if another auth request comes, it's processed.
    ctx.SetTimeInTuPeriods(60000);
    ctx.HandleTimeout();
    ctx.SendClientAuthReqFrame();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    EXPECT_TRUE(device.wlan_queue.empty());
    auto auth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateIndOrdinal>);
    ASSERT_FALSE(auth_inds.empty());
    ctx.AssertAuthInd(std::move(*auth_inds.begin()));
}

TEST_F(ApInfraBssTest, DeauthenticateWhileAuthenticated) {
    ctx.StartAp();
    ctx.AuthenticateClient();

    // Send deauthentication frame
    ctx.SendClientDeauthFrame();

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto deauth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_FALSE(deauth_inds.empty());
    ctx.AssertDeauthInd(std::move(*deauth_inds.begin()),
                        wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);

    // Expect association context is still blank.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Associate_Success) {
    ctx.StartAp();
    ctx.AuthenticateClient();

    // Send association request frame
    ctx.SendClientAssocReqFrame();

    // Verify that an Association.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    ctx.AssertAssocInd(std::move(*assoc_inds.begin()));

    // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
    ctx.SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::SUCCESS, kAid);

    // Verify association response frame for the client
    // WLAN queue should have AssociateResponse and BlockAck request
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(2));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->status_code, status_code::kSuccess);
    EXPECT_EQ(frame.body()->aid, kAid);

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
    ctx.SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::SUCCESS, kAid);

    // Expect association context has been set properly.
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx->bssid, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(actual_ctx->aid, kAid);
    auto rates = bss.Rates();
    EXPECT_EQ(actual_ctx->rates_cnt, rates.size());
    for (size_t i = 0; i < rates.size(); i++) {
        EXPECT_EQ(actual_ctx->rates[i], rates[i]);
    }
    EXPECT_TRUE(actual_ctx->has_ht_cap);
    const wlan_ht_caps_t expected_ht_cap = BuildHtCapabilities(bss.Ht()).ToDdk();
    const wlan_ht_caps_t actual_ht_cap = actual_ctx->ht_cap;
    EXPECT_EQ(actual_ht_cap.ht_capability_info, expected_ht_cap.ht_capability_info);
    EXPECT_EQ(actual_ht_cap.ampdu_params, expected_ht_cap.ampdu_params);
    size_t len = sizeof(expected_ht_cap.supported_mcs_set) /
        sizeof(expected_ht_cap.supported_mcs_set[0]);
    for (size_t i = 0; i < len; i++) {
        EXPECT_EQ(actual_ht_cap.supported_mcs_set[i], expected_ht_cap.supported_mcs_set[i]);
    }
    EXPECT_EQ(actual_ht_cap.ht_ext_capabilities, expected_ht_cap.ht_ext_capabilities);
    EXPECT_EQ(actual_ht_cap.tx_beamforming_capabilities,
              expected_ht_cap.tx_beamforming_capabilities);
    EXPECT_EQ(actual_ht_cap.asel_capabilities, expected_ht_cap.asel_capabilities);
}

TEST_F(ApInfraBssTest, Associate_MultipleClients) {
    ctx.StartAp();
    Context client2_ctx(&device, &bss, common::MacAddr({0x22, 0x22, 0x22, 0x22, 0x22, 0x2}));

    ctx.AuthenticateAndAssociateClient(kAid);

    // Eth frame from client 2 is no-op since client 2 is not associated
    client2_ctx.SendEthFrame(kTestPayload);
    ASSERT_TRUE(device.wlan_queue.empty());

    uint16_t client2_aid = 5;
    client2_ctx.AuthenticateAndAssociateClient(client2_aid);

    // Test sending message to client 1
    ctx.SendEthFrame(kTestPayload);
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    device.wlan_queue.clear();

    // Test sending message to client 2
    client2_ctx.SendEthFrame(kTestPayload);
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    pkt = std::move(*device.wlan_queue.begin());
    frame = client2_ctx.TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, client2_ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
}

TEST_F(ApInfraBssTest, Associate_SmeRefuses) {
    ctx.StartAp();
    ctx.AuthenticateClient();

    // Send association request frame
    ctx.SendClientAssocReqFrame();

    // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
    ctx.SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH, 0);

    // Expect association context has not been set (blank).
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);

    // Verify association response frame for the client is a refusal
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->status_code, status_code::kRefusedCapabilitiesMismatch);
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
    device.svc_queue.clear();

    // No timeout yet, so nothing happens. Even if another assoc request comes, it's a no-op
    ctx.SetTimeInTuPeriods(59000);
    ctx.HandleTimeout();
    ctx.SendClientAssocReqFrame();
    EXPECT_TRUE(device.svc_queue.empty());
    EXPECT_TRUE(device.wlan_queue.empty());

    // Timeout triggers. Verify that if another assoc request comes, it's processed.
    ctx.SetTimeInTuPeriods(60000);
    ctx.HandleTimeout();
    ctx.SendClientAssocReqFrame();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    EXPECT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    ctx.AssertAssocInd(std::move(*assoc_inds.begin()));

    // Expect association context has been cleared.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Associate_EmptySsid) {
    ctx.StartAp(false);
    ctx.AuthenticateClient();

    // Send association request frame without an SSID
    auto ssid = Span<uint8_t>();
    ctx.SendClientAssocReqFrame(ssid, true);

    // Verify that no Association.indication msg is sent out
    ASSERT_TRUE(device.svc_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());

    // Send a valid association request frame
    ctx.SendClientAssocReqFrame();

    // Verify that an Association.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    ctx.AssertAssocInd(std::move(*assoc_inds.begin()));
}

TEST_F(ApInfraBssTest, Associate_EmptyRsn) {
    ctx.StartAp(false);
    ctx.AuthenticateClient();

    // Send association request frame
    ctx.SendClientAssocReqFrame(kSsid, false);

    // Verify that an Association.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    ctx.AssertAssocInd(std::move(*assoc_inds.begin()), false);
}

TEST_F(ApInfraBssTest, DeauthenticateWhileAssociated) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Send deauthentication frame
    ctx.SendClientDeauthFrame();

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto deauth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_FALSE(deauth_inds.empty());
    ctx.AssertDeauthInd(std::move(*deauth_inds.begin()),
                        wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);

    // Expect association context has been cleared.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Disassociate) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Send deauthentication frame
    ctx.SendClientDisassocFrame();

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto disassoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDisassociateIndOrdinal>);
    ASSERT_FALSE(disassoc_inds.empty());
    ctx.AssertDisassocInd(std::move(*disassoc_inds.begin()));

    // Expect association context has been cleared.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Exchange_Eapol_Frames) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Send MLME-EAPOL.request.
    ctx.SendEapolRequestMsg();

    // Verify MLME-EAPOL.confirm message was sent.
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto eapol_conf = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEEapolConfOrdinal>);
    ASSERT_FALSE(eapol_conf.empty());
    auto eapol_conf_pkt = std::move(*eapol_conf.begin());
    ASSERT_EQ(eapol_conf_pkt->peer(), Packet::Peer::kService);
    MlmeMsg<wlan_mlme::EapolConfirm> msg;
    auto status = MlmeMsg<wlan_mlme::EapolConfirm>::FromPacket(std::move(eapol_conf_pkt), &msg);
    ASSERT_EQ(status, ZX_OK);
    EXPECT_EQ(msg.body()->result_code, wlan_mlme::EapolResultCodes::SUCCESS);

    // Verify EAPOL frame was sent.
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, ctx.client_addr.byte, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->protocol_id, htobe16(kEapolProtocolId));
    auto type_checked_frame = frame.SkipHeader().CheckBodyType<EapolHdr>();
    ASSERT_TRUE(type_checked_frame);
    auto llc_eapol_frame = type_checked_frame.CheckLength();
    ASSERT_TRUE(llc_eapol_frame);
    EXPECT_EQ(llc_eapol_frame.body_len(), static_cast<size_t>(5));
    EXPECT_RANGES_EQ(llc_eapol_frame.body_data(), kEapolPdu);
}

TEST_F(ApInfraBssTest, SendFrameAfterAssociation) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Have BSS process Eth frame.
    ctx.SendEthFrame(kTestPayload);

    // Verify a data WLAN frame was sent.
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload);
}

TEST_F(ApInfraBssTest, UnprotectedApReceiveFramesAfterAssociation) {
    ctx.StartAp(false);

    // Simulate unauthenticated client sending data frames, which should be ignored
    ctx.SendDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    ctx.AuthenticateClient();
    ctx.SendDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    ctx.AssociateClient(kAid);
    ctx.SendDataFrame();
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    // Verify ethernet frame is sent out and is correct
    auto eth_frames = device.GetEthPackets();
    ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
    ctx.AssertEthFrame(std::move(*eth_frames.begin()), kTestPayload);
}

TEST_F(ApInfraBssTest, MlmeDeauthReqWhileAssociated) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Send MLME-DEAUTHENTICATE.request
    auto reason_code = wlan_mlme::ReasonCode::FOURWAY_HANDSHAKE_TIMEOUT;
    ctx.SendDeauthReqMsg(reason_code);

    // Verify MLME-DEAUTHENTICATE.confirm message was sent
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto deauth_conf = device.GetServicePackets(
        IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateConfOrdinal>);
    ASSERT_FALSE(deauth_conf.empty());
    auto pkt = std::move(*deauth_conf.begin());
    ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
    MlmeMsg<wlan_mlme::DeauthenticateConfirm> msg;
    auto status = MlmeMsg<wlan_mlme::DeauthenticateConfirm>::FromPacket(std::move(pkt), &msg);
    ASSERT_EQ(status, ZX_OK);

    // Verify deauthenticate frame was sent
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    pkt = std::move(*device.wlan_queue.begin());
    auto frame = ctx.TypeCheckWlanFrame<MgmtFrameView<Deauthentication>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->reason_code, static_cast<uint16_t>(reason_code));
}

TEST_F(ApInfraBssTest, SetKeys) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Send MLME-SETKEYS.request
    ctx.SendSetKeysRequestMsg();

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

    // Send MLME-SETKEYS.request
    ctx.SendSetKeysRequestMsg();

    EXPECT_TRUE(device.GetKeys().empty());
}

TEST_F(ApInfraBssTest, PowerSaving_IgnoredBeforeControlledPortOpens) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);

    // Simulate client sending null data frame with power saving.
    auto pwr_mgmt = true;
    ctx.SendNullDataFrame(pwr_mgmt);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

    // Two Ethernet frames arrive. WLAN frame is sent out since we ignored previous frame and did
    // not change client's status to dozing
    std::vector<uint8_t> payload2 = {'m', 's', 'g', '2'};
    ctx.SendEthFrame(kTestPayload);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload);
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
                                    {.more_data = 1, .protected_frame = 1});
    pkt = std::move(*(device.wlan_queue.begin() + 1));
    ctx.AssertDataFrameSentToClient(std::move(pkt), payload2, {.protected_frame = 1});
}

TEST_F(ApInfraBssTest, PowerSaving_UnprotectedAp) {
    // For unprotected AP, power saving should work as soon as client is associated
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
    ctx.AssertDataFrameSentToClient(std::move(pkt), kTestPayload, {.more_data = 1});
    pkt = std::move(*(device.wlan_queue.begin() + 1));
    ctx.AssertDataFrameSentToClient(std::move(pkt), payload2);
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
    ctx.SendDataFrame();

    // For protected AP, controlled port is not opened until RSNA is established, so data frame
    // should be ignored
    EXPECT_TRUE(device.eth_queue.empty());
}

TEST_F(ApInfraBssTest, ReceiveFrames_AfterControlledPortOpens) {
    ctx.StartAp();
    ctx.AuthenticateAndAssociateClient(kAid);
    ctx.EstablishRsna();

    // Simulate client sending data frame to AP
    ASSERT_TRUE(device.eth_queue.empty());
    ctx.SendDataFrame();

    // Verify ethernet frame is sent out and is correct
    auto eth_frames = device.GetEthPackets();
    ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
    ctx.AssertEthFrame(std::move(*eth_frames.begin()), kTestPayload);
}

}  // namespace
}  // namespace wlan
