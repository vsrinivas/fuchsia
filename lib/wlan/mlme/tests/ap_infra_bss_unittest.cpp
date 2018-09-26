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

    void SetUp() override {
        StartAp();
        ASSERT_TRUE(device.svc_queue.empty());
        ASSERT_TRUE(device.wlan_queue.empty());
    }

    void TearDown() override { bss.Stop(); }

    zx_status_t SendStartReqMsg() {
        MlmeMsg<wlan_mlme::StartRequest> start_req;
        auto status = CreateStartRequest(&start_req);
        if (status != ZX_OK) { return status; }
        bss.Start(fbl::move(start_req));
        return ZX_OK;
    }

    zx_status_t SendClientAuthReqFrame() {
        return HandleFrame([](auto pkt) { return CreateAuthReqFrame(pkt); });
    }

    zx_status_t SendClientAssocReqFrame() {
        return HandleFrame([](auto pkt) { return CreateAssocReqFrame(pkt); });
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

    void StartAp() {
        SendStartReqMsg();
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
        AuthenticateClient();

        SendClientAssocReqFrame();
        SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::SUCCESS);
        device.svc_queue.clear();
        device.wlan_queue.clear();
    }

    void AssertAuthInd(fbl::unique_ptr<Packet> pkt) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::AuthenticateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::AuthenticateIndication>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(msg.body()->auth_type, wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), kClientAddress, 6), 0);
    }

    void AssertAssocInd(fbl::unique_ptr<Packet> pkt) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        MlmeMsg<wlan_mlme::AssociateIndication> msg;
        auto status = MlmeMsg<wlan_mlme::AssociateIndication>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);

        EXPECT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), kClientAddress, 6), 0);
        EXPECT_EQ(msg.body()->listen_interval, kListenInterval);
        EXPECT_EQ(std::memcmp(msg.body()->ssid->data(), kSsid, msg.body()->ssid->size()), 0);
        EXPECT_EQ(std::memcmp(msg.body()->rsn->data(), kRsne, sizeof(kRsne)), 0);
    }

    MockDevice device;
    InfraBss bss;
};

TEST_F(ApInfraBssTest, StartAp) {
    // AP started in `SetUp`
    ASSERT_TRUE(bss.IsStarted());
}

TEST_F(ApInfraBssTest, Authenticate_Success) {
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
    // Send authentication request frame
    SendClientAuthReqFrame();
    device.svc_queue.empty();

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

TEST_F(ApInfraBssTest, Associate_Success) {
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
}

TEST_F(ApInfraBssTest, Associate_SmeRefuses) {
    AuthenticateClient();

    // Send association request frame
    SendClientAssocReqFrame();

    // Simulate SME sending MLME-ASSOCIATE.response msg with a success code
    SendAssocResponseMsg(wlan_mlme::AssociateResultCodes::REFUSED_CAPABILITIES_MISMATCH);

    // Verify association response frame for the client is a refusal
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
    auto frame = TypeCheckWlanFrame<MgmtFrameView<AssociationResponse>>(pkt.get());
    EXPECT_EQ(std::memcmp(frame.hdr()->addr1.byte, kClientAddress, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr2.byte, kBssid1, 6), 0);
    EXPECT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    EXPECT_EQ(frame.body()->status_code, status_code::kRefused);
    EXPECT_EQ(frame.body()->aid, kUnknownAid);
}

TEST_F(ApInfraBssTest, Exchange_Eapol_Frames) {
    AssociateClient();

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

}  // namespace
}  // namespace wlan
