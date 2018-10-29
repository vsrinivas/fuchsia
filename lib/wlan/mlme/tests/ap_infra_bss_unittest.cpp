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

#include <gtest/gtest.h>

namespace wlan {

namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

const std::vector<uint8_t> kTestPayload = {'F', 'u', 'c', 'h', 's', 'i', 'a'};

struct ApInfraBssTest : public ::testing::Test {
    ApInfraBssTest()
        : bss(&device, fbl::make_unique<BeaconSender>(&device), common::MacAddr(kBssid1)) {}

    zx_status_t HandleFrame(std::function<zx_status_t(fbl::unique_ptr<Packet>*)> create_frame) {
        fbl::unique_ptr<Packet> pkt;
        auto status = create_frame(&pkt);
        if (status != ZX_OK) { return status; }
        bss.HandleAnyFrame(fbl::move(pkt));
        return ZX_OK;
    }

    template <typename M>
    zx_status_t HandleMlmeMsg(std::function<zx_status_t(MlmeMsg<M>*)> create_msg) {
        MlmeMsg<M> msg;
        auto status = create_msg(&msg);
        if (status != ZX_OK) { return status; }
        bss.HandleMlmeMsg(msg);
        return ZX_OK;
    }

    template <typename FV> FV TypeCheckWlanFrame(Packet* pkt) {
        EXPECT_EQ(pkt->peer(), Packet::Peer::kWlan);
        auto type_checked_frame = FV::CheckType(pkt);
        EXPECT_TRUE(type_checked_frame);
        auto frame = type_checked_frame.CheckLength();
        EXPECT_TRUE(frame);
        return frame;
    }

    void SetUp() override { device.SetTime(zx::time(0)); }
    void TearDown() override { bss.Stop(); }

    zx_status_t SendStartReqMsg(bool protected_ap) {
        MlmeMsg<wlan_mlme::StartRequest> start_req;
        auto status = CreateStartRequest(&start_req, protected_ap);
        if (status != ZX_OK) { return status; }
        bss.Start(fbl::move(start_req));
        return ZX_OK;
    }

    zx_status_t SendClientAuthReqFrame() {
        return HandleFrame([](auto pkt) { return CreateAuthReqFrame(pkt); });
    }

    zx_status_t SendClientDeauthFrame() {
        return HandleFrame([](auto pkt) { return CreateDeauthFrame(pkt); });
    }

    zx_status_t SendClientAssocReqFrame(Span<const uint8_t> ssid = kSsid, bool rsn = true) {
        return HandleFrame([=](auto pkt) { return CreateAssocReqFrame(pkt, ssid, rsn); });
    }

    zx_status_t SendClientDisassocFrame() {
        return HandleFrame([](auto pkt) { return CreateDisassocFrame(pkt); });
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
            frame.hdr()->addr2 = common::MacAddr(kClientAddress);
            frame.hdr()->addr3 = bssid;
            *pkt = frame.Take();
            return ZX_OK;
        });
    }

    zx_status_t SendDataFrame() {
        return HandleFrame([](auto pkt) {
            auto frame = CreateDataFrame(kTestPayload.data(), kTestPayload.size());
            if (frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }

            common::MacAddr bssid(kBssid1);
            frame.hdr()->fc.set_from_ds(0);
            frame.hdr()->fc.set_to_ds(1);
            frame.hdr()->addr1 = bssid;
            frame.hdr()->addr2 = common::MacAddr(kClientAddress);
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
            eth_frame.hdr()->dest = common::MacAddr(kClientAddress);
            *pkt = eth_frame.Take();
            return ZX_OK;
        });
    }

    zx_status_t SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes result_code) {
        return HandleMlmeMsg<wlan_mlme::AuthenticateResponse>(
            [=](auto msg) { return CreateAuthResponse(msg, result_code); });
    }

    zx_status_t SendAssocResponseMsg(wlan_mlme::AssociateResultCodes result_code) {
        return HandleMlmeMsg<wlan_mlme::AssociateResponse>(
            [=](auto msg) { return CreateAssocResponse(msg, result_code); });
    }

    zx_status_t SendEapolRequestMsg() {
        return HandleMlmeMsg<wlan_mlme::EapolRequest>(
            [=](auto msg) { return CreateEapolRequest(msg); });
    }

    zx_status_t SendSetKeysRequestMsg() {
        return HandleMlmeMsg<wlan_mlme::SetKeysRequest>([](auto msg) {
            auto key_data = std::vector(kKeyData, kKeyData + sizeof(kKeyData));
            return CreateSetKeysRequest(msg, key_data, wlan_mlme::KeyType::PAIRWISE);
        });
    }

    zx::duration TuPeriodsToDuration(size_t periods) { return zx::usec(1024) * periods; }

    void SetTimeInTuPeriods(size_t periods) {
        device.SetTime(zx::time(0) + TuPeriodsToDuration(periods));
    }

    void StartAp(bool protected_ap = true) {
        SendStartReqMsg(protected_ap);
        device.svc_queue.clear();
        device.wlan_queue.clear();
    }

    void AuthenticateClient() {
        SendClientAuthReqFrame();
        SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes::SUCCESS);
        device.svc_queue.clear();
        device.wlan_queue.clear();
    }

    void AssociateClient() {
        SendClientAssocReqFrame();
        SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::SUCCESS);
        device.svc_queue.clear();
        device.wlan_queue.clear();
    }

    void AuthenticateAndAssociateClient() {
        AuthenticateClient();
        AssociateClient();
    }

    void EstablishRsna() {
        // current implementation naively assumes that RSNA is established as soon as one key is set
        SendSetKeysRequestMsg();
    }

    void AssertAuthInd(fbl::unique_ptr<Packet> pkt) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::AuthenticateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::AuthenticateIndication>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(msg.body()->auth_type, wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), kClientAddress, 6), 0);
    }

    void AssertDeauthInd(fbl::unique_ptr<Packet> pkt, wlan_mlme::ReasonCode reason_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::DeauthenticateIndication> msg;
        auto status =
            MlmeMsg<wlan_mlme::DeauthenticateIndication>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(msg.body()->reason_code, reason_code);
    }

    void AssertAssocInd(fbl::unique_ptr<Packet> pkt, bool rsn = true) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::AssociateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::AssociateIndication>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), kClientAddress, 6), 0);
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
        auto status = MlmeMsg<wlan_mlme::DisassociateIndication>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

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
        EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
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
        EXPECT_EQ(std::memcmp(frame.hdr()->src.byte, kClientAddress, 6), 0);
        EXPECT_EQ(std::memcmp(frame.hdr()->dest.byte, kBssid1, 6), 0);
        EXPECT_EQ(frame.hdr()->ether_type, 42);
        EXPECT_EQ(frame.body_len(), expected_payload.size());
        EXPECT_EQ(std::memcmp(frame.body()->data, expected_payload.data(), expected_payload.size()),
                  0);
    }

    MockDevice device;
    InfraBss bss;
};

TEST_F(ApInfraBssTest, StartAp) {
    StartAp();
    ASSERT_TRUE(bss.IsStarted());
}

TEST_F(ApInfraBssTest, Authenticate_Success) {
    StartAp();

    // Send authentication request frame
    SendClientAuthReqFrame();

    // Verify that an Authentication.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto auth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateIndOrdinal>);
    ASSERT_FALSE(auth_inds.empty());
    AssertAuthInd(fbl::move(*auth_inds.begin()));

    // Simulate SME sending MLME-AUTHENTICATE.response msg with a success code
    SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes::SUCCESS);
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    fbl::unique_ptr<Packet> pkt = std::move(*device.wlan_queue.begin());
    auto frame = TypeCheckWlanFrame<MgmtFrameView<Authentication>>(pkt.get());

    // Verify authentication response frame for the client
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
    EXPECT_EQ(frame.body()->auth_txn_seq_number, 2);
    EXPECT_EQ(frame.body()->status_code, status_code::kSuccess);
}

TEST_F(ApInfraBssTest, Authenticate_SmeRefuses) {
    StartAp();

    // Send authentication request frame
    SendClientAuthReqFrame();

    // Simulate SME sending MLME-AUTHENTICATE.response msg with a refusal code
    SendAuthResponseMsg(wlan_mlme::AuthenticateResultCodes::REFUSED);

    // Verify that authentication response frame for client is a refusal
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = TypeCheckWlanFrame<MgmtFrameView<Authentication>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
    EXPECT_EQ(frame.body()->auth_txn_seq_number, 2);
    EXPECT_EQ(frame.body()->status_code, status_code::kRefused);
}

TEST_F(ApInfraBssTest, Authenticate_Timeout) {
    StartAp();

    // Send authentication request frame
    SendClientAuthReqFrame();
    device.svc_queue.clear();

    // No timeout yet, so nothing happens. Even if another auth request comes, it's a no-op
    SetTimeInTuPeriods(59000);
    bss.HandleTimeout(common::MacAddr(kClientAddress));
    SendClientAuthReqFrame();
    EXPECT_TRUE(device.svc_queue.empty());
    EXPECT_TRUE(device.wlan_queue.empty());

    // Timeout triggers. Verify that if another auth request comes, it's processed.
    SetTimeInTuPeriods(60000);
    bss.HandleTimeout(common::MacAddr(kClientAddress));
    SendClientAuthReqFrame();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    EXPECT_TRUE(device.wlan_queue.empty());
    auto auth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateIndOrdinal>);
    ASSERT_FALSE(auth_inds.empty());
    AssertAuthInd(fbl::move(*auth_inds.begin()));
}

TEST_F(ApInfraBssTest, DeauthenticateWhileAuthenticated) {
    StartAp();
    AuthenticateClient();

    // Send deauthentication frame
    SendClientDeauthFrame();

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto deauth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_FALSE(deauth_inds.empty());
    AssertDeauthInd(fbl::move(*deauth_inds.begin()), wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);

    // Expect association context is still blank.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Associate_Success) {
    StartAp();
    AuthenticateClient();

    // Send association request frame
    SendClientAssocReqFrame();

    // Verify that an Association.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    AssertAssocInd(fbl::move(*assoc_inds.begin()));

    // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
    SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::SUCCESS);

    // Expect association context has been set properly.
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx->bssid, kClientAddress, 6), 0);
    EXPECT_EQ(actual_ctx->aid, kAid);
    const SupportedRate* rates;
    size_t num_rates;
    rates = bss.Rates(&num_rates);
    EXPECT_EQ(actual_ctx->supported_rates_cnt, num_rates);
    for (size_t i = 0; i < num_rates; i++) {
        EXPECT_EQ(actual_ctx->supported_rates[i], rates[i]);
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

    // Verify association response frame for the client
    // WLAN queue should have AssociateResponse and BlockAck request
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(2));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->status_code, status_code::kSuccess);
    EXPECT_EQ(frame.body()->aid, kAid);

    device.wlan_queue.clear();
    SendEthFrame(kTestPayload);
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
}

TEST_F(ApInfraBssTest, Associate_SmeRefuses) {
    StartAp();
    AuthenticateClient();

    // Send association request frame
    SendClientAssocReqFrame();

    // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
    SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH);

    // Expect association context has not been set (blank).
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);

    // Verify association response frame for the client is a refusal
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->status_code, status_code::kRefused);
    EXPECT_EQ(frame.body()->aid, kUnknownAid);

    device.wlan_queue.clear();
    // Sending frame should be a no-op since association fails
    SendEthFrame(kTestPayload);
    EXPECT_TRUE(device.wlan_queue.empty());
}

TEST_F(ApInfraBssTest, Associate_Timeout) {
    StartAp();
    AuthenticateClient();

    // Send association request frame
    SendClientAssocReqFrame();
    device.svc_queue.clear();

    // No timeout yet, so nothing happens. Even if another assoc request comes, it's a no-op
    SetTimeInTuPeriods(59000);
    bss.HandleTimeout(common::MacAddr(kClientAddress));
    SendClientAssocReqFrame();
    EXPECT_TRUE(device.svc_queue.empty());
    EXPECT_TRUE(device.wlan_queue.empty());

    // Timeout triggers. Verify that if another assoc request comes, it's processed.
    SetTimeInTuPeriods(60000);
    bss.HandleTimeout(common::MacAddr(kClientAddress));
    SendClientAssocReqFrame();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    EXPECT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    AssertAssocInd(fbl::move(*assoc_inds.begin()));

    // Expect association context has been cleared.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Associate_EmptySsid) {
    StartAp(false);
    AuthenticateClient();

    // Send association request frame without an SSID
    auto ssid = Span<uint8_t>();
    SendClientAssocReqFrame(ssid, true);

    // Verify that no Association.indication msg is sent out
    ASSERT_TRUE(device.svc_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());

    // Send a valid association request frame
    SendClientAssocReqFrame();

    // Verify that an Association.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    AssertAssocInd(fbl::move(*assoc_inds.begin()));
}

TEST_F(ApInfraBssTest, Associate_EmptyRsn) {
    StartAp(false);
    AuthenticateClient();

    // Send association request frame
    SendClientAssocReqFrame(kSsid, false);

    // Verify that an Association.indication msg is sent out (to SME)
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    auto assoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_FALSE(assoc_inds.empty());
    AssertAssocInd(fbl::move(*assoc_inds.begin()), false);
}

TEST_F(ApInfraBssTest, DeauthenticateWhileAssociated) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Send deauthentication frame
    SendClientDeauthFrame();

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto deauth_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_FALSE(deauth_inds.empty());
    AssertDeauthInd(fbl::move(*deauth_inds.begin()), wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);

    // Expect association context has been cleared.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Disassociate) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Send deauthentication frame
    SendClientDisassocFrame();

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto disassoc_inds =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDisassociateIndOrdinal>);
    ASSERT_FALSE(disassoc_inds.empty());
    AssertDisassocInd(fbl::move(*disassoc_inds.begin()));

    // Expect association context has been cleared.
    wlan_assoc_ctx_t expected_ctx = {};
    const wlan_assoc_ctx_t* actual_ctx = device.GetStationAssocContext();
    EXPECT_EQ(std::memcmp(actual_ctx, &expected_ctx, sizeof(expected_ctx)), 0);
}

TEST_F(ApInfraBssTest, Exchange_Eapol_Frames) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Send MLME-EAPOL.request.
    SendEapolRequestMsg();

    // Verify MLME-EAPOL.confirm message was sent.
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto eapol_conf = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEEapolConfOrdinal>);
    ASSERT_FALSE(eapol_conf.empty());
    auto eapol_conf_pkt = fbl::move(*eapol_conf.begin());
    ASSERT_EQ(eapol_conf_pkt->peer(), Packet::Peer::kService);
    MlmeMsg<wlan_mlme::EapolConfirm> msg;
    auto status = MlmeMsg<wlan_mlme::EapolConfirm>::FromPacket(fbl::move(eapol_conf_pkt), &msg);
    ASSERT_EQ(status, ZX_OK);
    EXPECT_EQ(msg.body()->result_code, wlan_mlme::EapolResultCodes::SUCCESS);

    // Verify EAPOL frame was sent.
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = TypeCheckWlanFrame<DataFrameView<LlcHeader>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->protocol_id, htobe16(kEapolProtocolId));
    auto type_checked_frame = frame.SkipHeader().CheckBodyType<EapolHdr>();
    ASSERT_TRUE(type_checked_frame);
    auto llc_eapol_frame = type_checked_frame.CheckLength();
    ASSERT_TRUE(llc_eapol_frame);
    EXPECT_EQ(llc_eapol_frame.body_len(), static_cast<size_t>(5));
    EXPECT_EQ(std::memcmp(llc_eapol_frame.body_data(), kEapolPdu, sizeof(kEapolPdu)), 0);
}

TEST_F(ApInfraBssTest, SendFrameAfterAssociation) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Have BSS process Eth frame.
    SendEthFrame(kTestPayload);

    // Verify a data WLAN frame was sent.
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    AssertDataFrameSentToClient(fbl::move(pkt), kTestPayload);
}

TEST_F(ApInfraBssTest, UnprotectedApReceiveFramesAfterAssociation) {
    StartAp(false);

    // Simulate unauthenticated client sending data frames, which should be ignored
    SendDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    AuthenticateClient();
    SendDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    AssociateClient();
    SendDataFrame();
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    // Verify ethernet frame is sent out and is correct
    auto eth_frames = device.GetEthPackets();
    ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
    AssertEthFrame(std::move(*eth_frames.begin()), kTestPayload);
}

TEST_F(ApInfraBssTest, SetKeys) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Send MLME-SETKEYS.request
    SendSetKeysRequestMsg();

    ASSERT_EQ(device.GetKeys().size(), static_cast<size_t>(1));
    auto key_config = fbl::move(device.GetKeys()[0]);
    EXPECT_EQ(std::memcmp(key_config.key, kKeyData, sizeof(kKeyData)), 0);
    EXPECT_EQ(key_config.key_idx, 1);
    EXPECT_EQ(key_config.key_type, WLAN_KEY_TYPE_PAIRWISE);
    EXPECT_EQ(std::memcmp(key_config.peer_addr, kClientAddress, sizeof(kClientAddress)), 0);
    EXPECT_EQ(std::memcmp(key_config.cipher_oui, kCipherOui, sizeof(kCipherOui)), 0);
    EXPECT_EQ(key_config.cipher_type, kCipherSuiteType);
}

TEST_F(ApInfraBssTest, SetKeys_IgnoredForUnprotectedAp) {
    StartAp(false);
    AuthenticateAndAssociateClient();

    // Send MLME-SETKEYS.request
    SendSetKeysRequestMsg();

    EXPECT_TRUE(device.GetKeys().empty());
}

TEST_F(ApInfraBssTest, PowerSaving_IgnoredBeforeControlledPortOpens) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Simulate client sending null data frame with power saving.
    auto pwr_mgmt = true;
    SendNullDataFrame(pwr_mgmt);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

    // Two Ethernet frames arrive. WLAN frame is sent out since we ignored previous frame and did
    // not change client's status to dozing
    std::vector<uint8_t> payload2 = {'m', 's', 'g', '2'};
    SendEthFrame(kTestPayload);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    AssertDataFrameSentToClient(fbl::move(pkt), kTestPayload);
}

TEST_F(ApInfraBssTest, PowerSaving_AfterControlledPortOpens) {
    StartAp();
    AuthenticateAndAssociateClient();
    EstablishRsna();

    // Simulate client sending null data frame with power saving.
    auto pwr_mgmt = true;
    SendNullDataFrame(pwr_mgmt);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

    // Two Ethernet frames arrive. Verify no WLAN frame is sent out yet.
    std::vector<uint8_t> payload2 = {'m', 's', 'g', '2'};
    SendEthFrame(kTestPayload);
    SendEthFrame(payload2);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

    // Client notifies that it wakes up. Buffered frames should be sent out now
    SendNullDataFrame(!pwr_mgmt);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(2));
    auto pkt = std::move(*device.wlan_queue.begin());
    AssertDataFrameSentToClient(fbl::move(pkt), kTestPayload,
                                {.more_data = 1, .protected_frame = 1});
    pkt = std::move(*(device.wlan_queue.begin() + 1));
    AssertDataFrameSentToClient(fbl::move(pkt), payload2, {.protected_frame = 1});
}

TEST_F(ApInfraBssTest, PowerSaving_UnprotectedAp) {
    // For unprotected AP, power saving should work as soon as client is associated
    StartAp(false);
    AuthenticateAndAssociateClient();

    // Simulate client sending null data frame with power saving.
    auto pwr_mgmt = true;
    SendNullDataFrame(pwr_mgmt);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

    // Two Ethernet frames arrive. Verify no WLAN frame is sent out yet.
    std::vector<uint8_t> payload2 = {'m', 's', 'g', '2'};
    SendEthFrame(kTestPayload);
    SendEthFrame(payload2);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(0));

    // Client notifies that it wakes up. Buffered frames should be sent out now
    SendNullDataFrame(!pwr_mgmt);
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(2));
    auto pkt = std::move(*device.wlan_queue.begin());
    AssertDataFrameSentToClient(fbl::move(pkt), kTestPayload, {.more_data = 1});
    pkt = std::move(*(device.wlan_queue.begin() + 1));
    AssertDataFrameSentToClient(fbl::move(pkt), payload2);
}

TEST_F(ApInfraBssTest, OutboundFramesAreProtectedAfterControlledPortOpens) {
    StartAp();
    AuthenticateAndAssociateClient();
    EstablishRsna();

    // Have BSS process Eth frame.
    SendEthFrame(kTestPayload);

    // Verify a data WLAN frame was sent with protected frame flag set
    EXPECT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    AssertDataFrameSentToClient(fbl::move(pkt), kTestPayload, {.protected_frame = 1});
}

TEST_F(ApInfraBssTest, ReceiveFrames_BeforeControlledPortOpens) {
    StartAp();
    AuthenticateAndAssociateClient();

    // Simulate client sending data frame to AP
    ASSERT_TRUE(device.eth_queue.empty());
    SendDataFrame();

    // For protected AP, controlled port is not opened until RSNA is established, so data frame
    // should be ignored
    EXPECT_TRUE(device.eth_queue.empty());
}

TEST_F(ApInfraBssTest, ReceiveFrames_AfterControlledPortOpens) {
    StartAp();
    AuthenticateAndAssociateClient();
    EstablishRsna();

    // Simulate client sending data frame to AP
    ASSERT_TRUE(device.eth_queue.empty());
    SendDataFrame();

    // Verify ethernet frame is sent out and is correct
    auto eth_frames = device.GetEthPackets();
    ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
    AssertEthFrame(std::move(*eth_frames.begin()), kTestPayload);
}

}  // namespace
}  // namespace wlan
