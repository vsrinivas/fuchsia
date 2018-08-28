// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/clock.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/service.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include "mock_device.h"
#include "test_bss.h"

#include <gtest/gtest.h>

namespace wlan {

namespace {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

static constexpr uint8_t kTestPayload[] = "Hello Fuchsia";

struct MockChannelScheduler : public ChannelScheduler, public OnChannelHandler {
    MockChannelScheduler(MockDevice* device) :
            ChannelScheduler(this, device, device->CreateTimer(0)) {}

    void HandleOnChannelFrame(fbl::unique_ptr<Packet>) override {}
    void PreSwitchOffChannel() override {}
    void ReturnedOnChannel() override {}
};


struct ClientTest : public ::testing::Test {
    ClientTest() : chan_sched(MockChannelScheduler(&device)),
                   station(&device, device.CreateTimer(1), &chan_sched) {}

    template <typename M> zx_status_t SendMlmeMsg() {
        MlmeMsg<M> msg;
        auto status = CreateMlmeMsg<M>(&msg);
        if (status != ZX_OK) { return status; }
        station.HandleAnyMlmeMsg(msg);
        return ZX_OK;
    }

    template <typename F> zx_status_t SendFrame() {
        fbl::unique_ptr<Packet> pkt;
        auto status = CreateFrame<F>(&pkt);
        if (status != ZX_OK) { return status; }
        station.HandleAnyFrame(fbl::move(pkt));
        return ZX_OK;
    }

    zx_status_t SendDataFrame() {
        fbl::unique_ptr<Packet> pkt;
        auto status = CreateDataFrame(&pkt, kTestPayload, sizeof(kTestPayload));
        if (status != ZX_OK) { return status; }
        station.HandleAnyFrame(fbl::move(pkt));
        return ZX_OK;
    }

    zx_status_t SendNullDataFrame() {
        fbl::unique_ptr<Packet> pkt;
        auto status = CreateNullDataFrame(&pkt);
        if (status != ZX_OK) { return status; }
        station.HandleAnyFrame(fbl::move(pkt));
        return ZX_OK;
    }

    uint32_t MlmeMsgOrdinal(const Packet* pkt) {
        auto hdr = FromBytes<fidl_message_header_t>(pkt->data(), pkt->len());
        if (hdr == nullptr) { return 0; }

        return hdr->ordinal;
    }

    void Join() {
        device.SetTime(zx::time(0));
        SendMlmeMsg<wlan_mlme::JoinRequest>();
        SendFrame<Beacon>();
        device.svc_queue.clear();
        station.HandleTimeout();
    }

    void Authenticate() {
        SendMlmeMsg<wlan_mlme::AuthenticateRequest>();
        SendFrame<Authentication>();
        device.svc_queue.clear();
        device.wlan_queue.clear();
        station.HandleTimeout();
    }

    void Associate() {
        SendMlmeMsg<wlan_mlme::AssociateRequest>();
        SendFrame<AssociationResponse>();
        device.svc_queue.clear();
        device.wlan_queue.clear();
        station.HandleTimeout();
    }

    void Connect() {
        Join();
        Authenticate();
        Associate();
        station.HandleTimeout();
    }

    void SetTimeInBeaconPeriods(size_t periods) {
        device.SetTime(zx::time(0) + zx::usec(1024) * (periods * kBeaconPeriodTu));
    }

    void AssertJoinConfirm(fbl::unique_ptr<Packet> pkt, wlan_mlme::JoinResultCodes result_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        ASSERT_EQ(MlmeMsgOrdinal(pkt.get()), fuchsia_wlan_mlme_MLMEJoinConfOrdinal);

        MlmeMsg<wlan_mlme::JoinConfirm> msg;
        auto status = MlmeMsg<wlan_mlme::JoinConfirm>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(msg.body()->result_code, result_code);
    }

    void AssertAuthConfirm(fbl::unique_ptr<Packet> pkt,
                           wlan_mlme::AuthenticateResultCodes result_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        ASSERT_EQ(MlmeMsgOrdinal(pkt.get()), fuchsia_wlan_mlme_MLMEAuthenticateConfOrdinal);

        MlmeMsg<wlan_mlme::AuthenticateConfirm> msg;
        auto status = MlmeMsg<wlan_mlme::AuthenticateConfirm>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(msg.body()->result_code, result_code);
    }

    void AssertAssocConfirm(fbl::unique_ptr<Packet> pkt, uint16_t aid,
                            wlan_mlme::AssociateResultCodes result_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);
        ASSERT_EQ(MlmeMsgOrdinal(pkt.get()), fuchsia_wlan_mlme_MLMEAssociateConfOrdinal);

        MlmeMsg<wlan_mlme::AssociateConfirm> msg;
        auto status = MlmeMsg<wlan_mlme::AssociateConfirm>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(msg.body()->association_id, aid);
        ASSERT_EQ(msg.body()->result_code, result_code);
    }

    ChannelScheduler chan_sched;
    MockDevice device;
    Station station;
};

TEST_F(ClientTest, Join) {
    device.SetTime(zx::time(0));
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Send JOIN.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::JoinRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Ensure station moved onto the BSS channel.
    ASSERT_EQ(device.state->channel().primary, kBssChannel.primary);

    // Respond with a Beacon frame and verify a JOIN.confirm message was sent.
    ASSERT_EQ(SendFrame<Beacon>(), ZX_OK);
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto join_pkt = device.svc_queue.Dequeue();
    AssertJoinConfirm(fbl::move(join_pkt), wlan_mlme::JoinResultCodes::SUCCESS);

    // Verify a delayed timeout won't cause another confirmation.
    device.svc_queue.clear();
    SetTimeInBeaconPeriods(100);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.is_empty());
}

TEST_F(ClientTest, Authenticate) {
    Join();

    // Send AUTHENTICATION.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AuthenticateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Verify wlan frame is correct.
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = device.wlan_queue.Dequeue();
    ASSERT_EQ(pkt->peer(), Packet::Peer::kWlan);
    auto type_checked_frame = MgmtFrameView<Authentication>::CheckType(pkt.get());
    ASSERT_TRUE(type_checked_frame);
    auto frame = type_checked_frame.CheckLength();
    ASSERT_TRUE(frame);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr1.byte, kBssid1, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr2.byte, kClientAddress, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    ASSERT_EQ(frame.body()->auth_algorithm_number, AuthAlgorithm::kOpenSystem);
    ASSERT_EQ(frame.body()->auth_txn_seq_number, 1);
    ASSERT_EQ(frame.body()->status_code, 0);

    // Respond with a Authentication frame and verify a AUTHENTICATION.confirm message was sent.
    ASSERT_EQ(SendFrame<Authentication>(), ZX_OK);
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto auth_pkt = device.svc_queue.Dequeue();
    AssertAuthConfirm(fbl::move(auth_pkt), wlan_mlme::AuthenticateResultCodes::SUCCESS);

    // Verify a delayed timeout won't cause another confirmation.
    device.svc_queue.clear();
    SetTimeInBeaconPeriods(100);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.is_empty());
}

TEST_F(ClientTest, Associate) {
    Join();
    Authenticate();

    // Send ASSOCIATE.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AssociateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Verify wlan frame is correct.
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = device.wlan_queue.Dequeue();
    ASSERT_EQ(pkt->peer(), Packet::Peer::kWlan);
    auto type_checked_frame = MgmtFrameView<AssociationRequest>::CheckType(pkt.get());
    ASSERT_TRUE(type_checked_frame);
    auto frame = type_checked_frame.CheckLength();
    ASSERT_TRUE(frame);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr1.byte, kBssid1, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr2.byte, kClientAddress, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
    size_t ie_len = frame.body_len() - frame.body()->len();
    ASSERT_TRUE(frame.body()->Validate(ie_len));

    // Respond with a Association Response frame and verify a ASSOCIATE.confirm message was sent.
    ASSERT_EQ(SendFrame<AssociationResponse>(), ZX_OK);
    ASSERT_FALSE(device.svc_queue.is_empty());
    auto assoc_pkt = device.svc_queue.Dequeue();
    AssertAssocConfirm(fbl::move(assoc_pkt), kAid, wlan_mlme::AssociateResultCodes::SUCCESS);

    // Verify a delayed timeout won't cause another confirmation.
    device.svc_queue.clear();
    SetTimeInBeaconPeriods(100);
    station.HandleTimeout();
    while (!device.svc_queue.is_empty()) {
        auto pkt = device.svc_queue.Dequeue();
        ASSERT_NE(MlmeMsgOrdinal(pkt.get()), fuchsia_wlan_mlme_MLMEAssociateConfOrdinal);
    }
}

TEST_F(ClientTest, JoinTimeout) {
    device.SetTime(zx::time(0));
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Send JOIN.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::JoinRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Timeout not yet hit.
    SetTimeInBeaconPeriods(kJoinTimeout - 1);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Timeout hit, verify a JOIN.confirm message was sent.
    SetTimeInBeaconPeriods(kJoinTimeout);
    station.HandleTimeout();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto join_pkt = device.svc_queue.Dequeue();
    AssertJoinConfirm(fbl::move(join_pkt), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
}

TEST_F(ClientTest, AuthTimeout) {
    Join();

    // Send AUTHENTICATE.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AuthenticateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Timeout not yet hit.
    SetTimeInBeaconPeriods(kAuthTimeout - 1);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Timeout hit, verify a AUTHENTICATION.confirm message was sent.
    SetTimeInBeaconPeriods(kAuthTimeout);
    station.HandleTimeout();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto auth_pkt = device.svc_queue.Dequeue();
    AssertAuthConfirm(fbl::move(auth_pkt),
                      wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
}

TEST_F(ClientTest, AssocTimeout) {
    Join();
    Authenticate();

    // Send ASSOCIATE.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AssociateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Timeout not yet hit.
    SetTimeInBeaconPeriods(10);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Timeout hit, verify a ASSOCIATE.confirm message was sent.
    SetTimeInBeaconPeriods(40);
    station.HandleTimeout();
    ASSERT_FALSE(device.svc_queue.is_empty());
    auto assoc_pkt = device.svc_queue.Dequeue();
    AssertAssocConfirm(fbl::move(assoc_pkt), 0,
                       wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY);
}

TEST_F(ClientTest, ExchangeDataAfterAssociation) {
    // Verify no data frame is exchanged before being associated.
    device.eth_queue.clear();
    SendDataFrame();
    SendNullDataFrame();
    ASSERT_TRUE(device.eth_queue.is_empty());
    ASSERT_TRUE(device.wlan_queue.is_empty());
    ASSERT_TRUE(device.svc_queue.is_empty());

    Join();
    SendDataFrame();
    SendNullDataFrame();
    ASSERT_TRUE(device.eth_queue.is_empty());
    ASSERT_TRUE(device.wlan_queue.is_empty());
    ASSERT_TRUE(device.svc_queue.is_empty());

    Authenticate();
    SendDataFrame();
    SendNullDataFrame();
    ASSERT_TRUE(device.eth_queue.is_empty());
    ASSERT_TRUE(device.wlan_queue.is_empty());
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Associate and send a data frame.
    Associate();
    SendDataFrame();
    ASSERT_EQ(device.eth_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.is_empty());
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Verify queued up ethernet frame is correct.
    auto pkt = device.eth_queue.Dequeue();
    ASSERT_EQ(pkt->peer(), Packet::Peer::kEthernet);
    EthFrameView frame(pkt.get());
    ASSERT_EQ(std::memcmp(frame.hdr()->src.byte, kClientAddress, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->dest.byte, kBssid1, 6), 0);
    ASSERT_EQ(frame.hdr()->ether_type, 42);
    ASSERT_EQ(frame.body_len(), sizeof(kTestPayload));
    ASSERT_EQ(std::memcmp(frame.body()->data, kTestPayload, sizeof(kTestPayload)), 0);

    // Send null data frame which shouldn't queue up any Ethernet frames but instead a "Keep Alive" one.
    SendNullDataFrame();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.svc_queue.is_empty());

    // Verify queued up "Keep Alive" frame is correct.
    pkt = device.wlan_queue.Dequeue();
    ASSERT_EQ(pkt->peer(), Packet::Peer::kWlan);
    auto partially_checked = DataFrameView<>::CheckType(pkt.get());
    ASSERT_TRUE(partially_checked);
    auto data_frame = partially_checked.CheckLength();
    ASSERT_EQ(data_frame.hdr()->fc.to_ds(), 1);
    ASSERT_EQ(data_frame.hdr()->fc.from_ds(), 0);
    ASSERT_EQ(std::memcmp(data_frame.hdr()->addr1.byte, kBssid1, 6), 0);
    ASSERT_EQ(std::memcmp(data_frame.hdr()->addr2.byte, kClientAddress, 6), 0);
    ASSERT_EQ(std::memcmp(data_frame.hdr()->addr3.byte, kBssid1, 6), 0);
    ASSERT_EQ(data_frame.body_len(), static_cast<size_t>(0));
}

TEST_F(ClientTest, DropManagementFrames) {
    Connect();

    // Construct and send deauthentication frame from another BSS.
    MgmtFrame<Deauthentication> frame;
    auto status = BuildMgmtFrame(&frame);
    ASSERT_EQ(status, ZX_OK);
    auto hdr = frame.hdr();
    hdr->addr1 = common::MacAddr(kBssid2);
    hdr->addr2 = common::MacAddr(kClientAddress);
    hdr->addr3 = common::MacAddr(kBssid2);
    auto deauth = frame.body();
    deauth->reason_code = 42;
    station.HandleAnyFrame(frame.Take());

    // Verify neither a management frame nor service message were sent.
    ASSERT_TRUE(device.svc_queue.is_empty());
    ASSERT_TRUE(device.wlan_queue.is_empty());
    ASSERT_TRUE(device.eth_queue.is_empty());

    // Verify data frames can still be send and the clientis presumably associated.
    SendDataFrame();
    ASSERT_EQ(device.eth_queue.size(), static_cast<size_t>(1));
}

TEST_F(ClientTest, SuccessiveJoin) {
    // Connect to a network
    Connect();

    // Issue a new MLME-JOIN.request which should reset the STA and restart the association flow.
    // Verify data frame exchange is not operational any longer.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::JoinRequest>(), ZX_OK);
    SendDataFrame();
    ASSERT_TRUE(device.eth_queue.is_empty());

    // Verify BSS was notified about deauthentication.
    // TODO(hahnr): This is currently not supported by the client.

    // Respond with a Beacon frame and verify a JOIN.confirm message was sent.
    ASSERT_EQ(SendFrame<Beacon>(), ZX_OK);
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto join_pkt = device.svc_queue.Dequeue();
    AssertJoinConfirm(fbl::move(join_pkt), wlan_mlme::JoinResultCodes::SUCCESS);
}

// Add additional tests for (tracked in NET-801):
// AP refuses Authentication/Association
// Regression tests for:
// - NET-898: PS-POLL after TIM indication.
// Deauthenticate in any state issued by AP/SME.
// Disassociation in any state issued by AP/SME.
// Handle Action frames and setup Block-Ack session.
// Drop data frames from unknown BSS.
// Handle AMSDUs.
// Connect to a:
// - protected network, exchange keys and send protected frames.
// - HT/VHT capable network
// - 5GHz network
// - different network than currently associated to
// Notify driver about association
// Ensure Deauthentication Indicaiton and notification is sent whenever deauthenticating.
// Enter/Leave power management when going off/on channel.
// Verify timeouts don't hit after resetting the station.

}  // namespace
}  // namespace wlan
