// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/timekeeper/clock.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/client/station.h>
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

static constexpr uint8_t kTestPayload[] = "Hello Fuchsia";

struct MockOnChannelHandler : public OnChannelHandler {
    void HandleOnChannelFrame(fbl::unique_ptr<Packet>) override {}
    void PreSwitchOffChannel() override {}
    void ReturnedOnChannel() override {}
};

struct MockOffChannelHandler : OffChannelHandler {
    void BeginOffChannelTime() override {}
    void HandleOffChannelFrame(fbl::unique_ptr<Packet>) override {}
    bool EndOffChannelTime(bool interrupted, OffChannelRequest* next_req) override { return false; }
};

struct ClientTest : public ::testing::Test {
    ClientTest()
        : chan_sched(ChannelScheduler(&on_channel_handler, &device, device.CreateTimer(0))),
          station(&device, device.CreateTimer(1), &chan_sched),
          off_channel_handler(MockOffChannelHandler()) {}

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
        station.HandleAnyWlanFrame(fbl::move(pkt));
        return ZX_OK;
    }

    zx_status_t SendDataFrame() {
        auto frame = CreateDataFrame(kTestPayload, sizeof(kTestPayload));
        if (frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }
        station.HandleAnyWlanFrame(frame.Take());
        return ZX_OK;
    }

    zx_status_t SendEmptyDataFrame() {
        auto frame = CreateDataFrame(nullptr, 0);
        if (frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }
        station.HandleAnyWlanFrame(frame.Take());
        return ZX_OK;
    }

    zx_status_t SendNullDataFrame() {
        auto frame = CreateNullDataFrame();
        if (frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }
        station.HandleAnyWlanFrame(frame.Take());
        return ZX_OK;
    }

    zx_status_t SendEthFrame() {
        auto eth_frame = CreateEthFrame(kTestPayload, sizeof(kTestPayload));
        if (eth_frame.IsEmpty()) { return ZX_ERR_NO_RESOURCES; }
        station.HandleEthFrame(EthFrame(eth_frame.Take()));
        return ZX_OK;
    }

    zx_status_t SendBeaconFrame(common::MacAddr bssid) {
        fbl::unique_ptr<Packet> pkt;
        auto status = CreateBeaconFrameWithBssid(&pkt, bssid);
        if (status != ZX_OK) { return status; }
        station.HandleAnyWlanFrame(fbl::move(pkt));
        return ZX_OK;
    }

    void Join() {
        device.SetTime(zx::time(0));
        SendMlmeMsg<wlan_mlme::JoinRequest>();
        SendFrame<Beacon>();
        device.svc_queue.clear();
        station.HandleTimeout();
        chan_sched.HandleTimeout();  // this is just to reset channel scheduler's state
    }

    void Authenticate() {
        SendMlmeMsg<wlan_mlme::AuthenticateRequest>();
        SendFrame<Authentication>();
        device.svc_queue.clear();
        device.wlan_queue.clear();
        station.HandleTimeout();
        chan_sched.HandleTimeout();
    }

    void Associate() {
        SendMlmeMsg<wlan_mlme::AssociateRequest>();
        SendFrame<AssociationResponse>();
        device.svc_queue.clear();
        device.wlan_queue.clear();
        station.HandleTimeout();
        chan_sched.HandleTimeout();
    }

    void Connect() {
        Join();
        Authenticate();
        Associate();
        station.HandleTimeout();
    }

    zx::duration BeaconPeriodsToDuration(size_t periods) {
        return zx::usec(1024) * (periods * kBeaconPeriodTu);
    }

    void SetTimeInBeaconPeriods(size_t periods) {
        device.SetTime(zx::time(0) + BeaconPeriodsToDuration(periods));
    }

    void IncreaseTimeByBeaconPeriods(size_t periods) {
        device.SetTime(device.GetTime() + BeaconPeriodsToDuration(periods));
    }

    void GoOffChannel() {
        // These numbers don't really affect below unit tests and were chosen arbitrarily.
        OffChannelRequest off_channel_request =
            OffChannelRequest{.chan = {.primary = 6, .cbw = CBW20, .secondary80 = 0},
                              .duration = zx::msec(200),
                              .handler = &off_channel_handler};
        chan_sched.RequestOffChannelTime(off_channel_request);
        station.PreSwitchOffChannel();
        device.wlan_queue.erase(device.wlan_queue.begin());  // dequeue the power-saving frame
        ASSERT_FALSE(chan_sched.OnChannel());                // sanity check
    }

    void GoBackToMainChannel() {
        chan_sched.EnsureOnChannel(device.GetTime() + BeaconPeriodsToDuration(1u));
        chan_sched.HandleTimeout();  // calling this just to reset channel scheduler's
                                     // `ensure_on_channel` flag.
        station.BackToMainChannel();
        device.wlan_queue.erase(device.wlan_queue.begin());  // dequeue the power-saving frame
        ASSERT_TRUE(chan_sched.OnChannel());                 // sanity check
    }

    void AssertJoinConfirm(fbl::unique_ptr<Packet> pkt, wlan_mlme::JoinResultCodes result_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);

        MlmeMsg<wlan_mlme::JoinConfirm> msg;
        auto status = MlmeMsg<wlan_mlme::JoinConfirm>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(msg.body()->result_code, result_code);
    }

    void AssertAuthConfirm(fbl::unique_ptr<Packet> pkt,
                           wlan_mlme::AuthenticateResultCodes result_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);

        MlmeMsg<wlan_mlme::AuthenticateConfirm> msg;
        auto status = MlmeMsg<wlan_mlme::AuthenticateConfirm>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(msg.body()->result_code, result_code);
    }

    void AssertAssocConfirm(fbl::unique_ptr<Packet> pkt, uint16_t aid,
                            wlan_mlme::AssociateResultCodes result_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kService);

        MlmeMsg<wlan_mlme::AssociateConfirm> msg;
        auto status = MlmeMsg<wlan_mlme::AssociateConfirm>::FromPacket(fbl::move(pkt), &msg);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_EQ(msg.body()->association_id, aid);
        ASSERT_EQ(msg.body()->result_code, result_code);
    }

    void AssertDeauthFrame(fbl::unique_ptr<Packet> pkt, wlan_mlme::ReasonCode reason_code) {
        ASSERT_EQ(pkt->peer(), Packet::Peer::kWlan);
        auto type_checked_frame = MgmtFrameView<Deauthentication>::CheckType(pkt.get());
        ASSERT_TRUE(type_checked_frame);
        auto frame = type_checked_frame.CheckLength();
        ASSERT_TRUE(frame);
        ASSERT_EQ(std::memcmp(frame.hdr()->addr1.byte, kBssid1, 6), 0);
        ASSERT_EQ(std::memcmp(frame.hdr()->addr2.byte, kClientAddress, 6), 0);
        ASSERT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);
        ASSERT_EQ(frame.body()->reason_code, static_cast<uint16_t>(reason_code));
    }

    MockDevice device;
    ChannelScheduler chan_sched;
    Station station;

    MockOnChannelHandler on_channel_handler;
    MockOffChannelHandler off_channel_handler;
};

TEST_F(ClientTest, Join) {
    device.SetTime(zx::time(0));
    ASSERT_TRUE(device.svc_queue.empty());

    // Send JOIN.request
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::JoinRequest>(), ZX_OK);

    // Ensure station moved onto the BSS channel.
    ASSERT_EQ(device.state->channel().primary, kBssChannel.primary);

    // Verify a JOIN.confirm message was sent.
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto joins = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEJoinConfOrdinal>);
    ASSERT_FALSE(joins.empty());
    AssertJoinConfirm(fbl::move(*joins.begin()), wlan_mlme::JoinResultCodes::SUCCESS);

    // Verify a delayed timeout won't cause another confirmation.
    device.svc_queue.clear();
    SetTimeInBeaconPeriods(100);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.empty());
}

TEST_F(ClientTest, Authenticate) {
    Join();

    // Send AUTHENTICATION.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AuthenticateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.empty());

    // Verify wlan frame is correct.
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
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
    auto auths = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateConfOrdinal>);
    ASSERT_FALSE(auths.empty());
    AssertAuthConfirm(fbl::move(*auths.begin()), wlan_mlme::AuthenticateResultCodes::SUCCESS);

    // Verify a delayed timeout won't cause another confirmation.
    device.svc_queue.clear();
    SetTimeInBeaconPeriods(100);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.empty());
}

TEST_F(ClientTest, Associate) {
    Join();
    Authenticate();

    // Send ASSOCIATE.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AssociateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.empty());

    // Verify wlan frame is correct.
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    auto pkt = std::move(*device.wlan_queue.begin());
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
    ASSERT_FALSE(device.svc_queue.empty());
    auto assocs = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateConfOrdinal>);
    ASSERT_EQ(assocs.size(), static_cast<size_t>(1));
    AssertAssocConfirm(fbl::move(*assocs.begin()), kAid, wlan_mlme::AssociateResultCodes::SUCCESS);

    // Verify a delayed timeout won't cause another confirmation.
    device.svc_queue.clear();
    SetTimeInBeaconPeriods(100);
    station.HandleTimeout();
    assocs = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateConfOrdinal>);
    ASSERT_TRUE(assocs.empty());
}

TEST_F(ClientTest, AuthTimeout) {
    Join();

    // Send AUTHENTICATE.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AuthenticateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.empty());

    // Timeout not yet hit.
    SetTimeInBeaconPeriods(kAuthTimeout - 1);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.empty());

    // Timeout hit, verify a AUTHENTICATION.confirm message was sent.
    SetTimeInBeaconPeriods(kAuthTimeout);
    station.HandleTimeout();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto auths = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateConfOrdinal>);
    ASSERT_FALSE(auths.empty());
    AssertAuthConfirm(fbl::move(*auths.begin()),
                      wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
}

TEST_F(ClientTest, AssocTimeout) {
    Join();
    Authenticate();

    // Send ASSOCIATE.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AssociateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.empty());

    // Timeout not yet hit.
    SetTimeInBeaconPeriods(10);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.empty());

    // Timeout hit, verify a ASSOCIATE.confirm message was sent.
    SetTimeInBeaconPeriods(40);
    station.HandleTimeout();
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto assocs = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateConfOrdinal>);
    ASSERT_FALSE(assocs.empty());
    AssertAssocConfirm(fbl::move(*assocs.begin()), 0,
                       wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY);
}

TEST_F(ClientTest, ExchangeDataAfterAssociation) {
    // Verify no data frame is exchanged before being associated.
    device.eth_queue.clear();
    SendDataFrame();
    SendNullDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    Join();
    SendDataFrame();
    SendNullDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    Authenticate();
    SendDataFrame();
    SendNullDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    // Associate and send a data frame.
    Associate();
    SendDataFrame();
    auto eth_frames = device.GetEthPackets();
    ASSERT_EQ(eth_frames.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.svc_queue.empty());

    // Verify queued up ethernet frame is correct.
    auto pkt = std::move(*eth_frames.begin());
    ASSERT_EQ(pkt->peer(), Packet::Peer::kEthernet);
    EthFrameView frame(pkt.get());
    ASSERT_EQ(std::memcmp(frame.hdr()->src.byte, kClientAddress, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->dest.byte, kBssid1, 6), 0);
    ASSERT_EQ(frame.hdr()->ether_type, 42);
    ASSERT_EQ(frame.body_len(), sizeof(kTestPayload));
    ASSERT_EQ(std::memcmp(frame.body()->data, kTestPayload, sizeof(kTestPayload)), 0);

    // Send null data frame which shouldn't queue up any Ethernet frames but instead a "Keep Alive"
    // one.
    SendNullDataFrame();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    ASSERT_TRUE(device.svc_queue.empty());

    // Verify queued up "Keep Alive" frame is correct.
    pkt = std::move(*device.wlan_queue.begin());
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

TEST_F(ClientTest, ProcessEmptyDataFrames) {
    Connect();

    // Send a data frame which carries an LLC frame with no payload.
    // Verify no ethernet frame was queued.
    SendEmptyDataFrame();
    ASSERT_TRUE(device.eth_queue.empty());
}

TEST_F(ClientTest, DropManagementFrames) {
    Connect();

    // Construct and send deauthentication frame from another BSS.
    MgmtFrame<Deauthentication> frame;
    auto status = CreateMgmtFrame(&frame);
    ASSERT_EQ(status, ZX_OK);
    auto hdr = frame.hdr();
    hdr->addr1 = common::MacAddr(kBssid2);
    hdr->addr2 = common::MacAddr(kClientAddress);
    hdr->addr3 = common::MacAddr(kBssid2);
    auto deauth = frame.body();
    deauth->reason_code = 42;
    station.HandleAnyWlanFrame(frame.Take());

    // Verify neither a management frame nor service message were sent.
    ASSERT_TRUE(device.svc_queue.empty());
    ASSERT_TRUE(device.wlan_queue.empty());
    ASSERT_TRUE(device.eth_queue.empty());

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
    ASSERT_TRUE(device.eth_queue.empty());

    // Verify BSS was notified about deauthentication.
    // TODO(hahnr): This is currently not supported by the client.

    // Respond with a Beacon frame and verify a JOIN.confirm message was sent.
    ASSERT_EQ(SendFrame<Beacon>(), ZX_OK);

    auto joins = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEJoinConfOrdinal>);
    ASSERT_EQ(joins.size(), static_cast<size_t>(1));
    AssertJoinConfirm(fbl::move(*joins.begin()), wlan_mlme::JoinResultCodes::SUCCESS);
}

TEST_F(ClientTest, AutoDeauth_NoBeaconReceived) {
    Connect();

    // Timeout not yet hit.
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout - 1);
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());
    auto deauths =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_TRUE(deauths.empty());

    // Auto-deauth timeout, client should be deauthenticated.
    IncreaseTimeByBeaconPeriods(1);
    station.HandleTimeout();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    AssertDeauthFrame(std::move(*device.wlan_queue.begin()),
                      wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);
    deauths = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_EQ(deauths.size(), static_cast<size_t>(1));
}

TEST_F(ClientTest, AutoDeauth_NoBeaconsShortlyAfterConnecting) {
    Connect();

    IncreaseTimeByBeaconPeriods(1);
    SendFrame<Beacon>();

    // Not enough time has passed yet since beacon frame was sent, so no deauth.
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout - 1);
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // Auto-deauth triggers now.
    IncreaseTimeByBeaconPeriods(1);
    station.HandleTimeout();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    AssertDeauthFrame(std::move(*device.wlan_queue.begin()),
                      wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);
    auto deauths =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_EQ(deauths.size(), static_cast<size_t>(1));
}

TEST_F(ClientTest, AutoDeauth_DoNotDeauthWhileSwitchingChannel) {
    Connect();

    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout - 1);
    GoOffChannel();

    // For next two timeouts, still off channel, so should not deauth.
    IncreaseTimeByBeaconPeriods(1);
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout);
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // Have not been back on main channel for long enough, so should not deauth.
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout);
    GoBackToMainChannel();
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // Before going off channel, we did not receive beacon for `kAutoDeauthTimeout - 1` period.
    // Now one more beacon period has passed after going back on channel, so should auto deauth.
    IncreaseTimeByBeaconPeriods(1);
    station.HandleTimeout();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    AssertDeauthFrame(std::move(*device.wlan_queue.begin()),
                      wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);
    auto deauths =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_EQ(deauths.size(), static_cast<size_t>(1));
}

TEST_F(ClientTest, AutoDeauth_InterleavingBeaconsAndChannelSwitches) {
    Connect();

    // Going off channel.
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout - 5);  // -- On-channel time without beacon -- //
    GoOffChannel();

    // No deauth since off channel.
    IncreaseTimeByBeaconPeriods(5);
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    IncreaseTimeByBeaconPeriods(1);
    GoBackToMainChannel();

    // Got beacon frame, which should reset the timeout.
    IncreaseTimeByBeaconPeriods(3);  // -- On-channel time without beacon  -- //
    SendFrame<Beacon>();             // -- Beacon timeout refresh -- ///

    // No deauth since beacon was received not too long ago.
    IncreaseTimeByBeaconPeriods(2);  // -- On-channel time without beacon  -- //
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // Going off channel and back on channel
    // Total on-channel time without beacons so far: 2 beacon intervals
    GoOffChannel();
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout);
    GoBackToMainChannel();

    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout - 3);  // -- On-channel time without beacon -- //
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // Going off channel and back on channel again
    // Total on-channel time without beacons so far: 2 + kAutoDeauthTimeout - 3
    GoOffChannel();
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout);
    GoBackToMainChannel();
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // One more beacon period and auto-deauth triggers
    IncreaseTimeByBeaconPeriods(1);  // -- On-channel time without beacon -- //
    station.HandleTimeout();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    AssertDeauthFrame(std::move(*device.wlan_queue.begin()),
                      wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);
    auto deauths =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_EQ(deauths.size(), static_cast<size_t>(1));
}

// This test explores what happens if the whole auto-deauth timeout duration is exhausted, but
// the client switches channel before auto-deauth can trigger. For the current implementation
// where we cancel timer when going off channel and reschedule when going back on channel,
// this test is intended to be a safeguard against making the mistake of scheduling or exactly
// in the present when going back on channel.
TEST_F(ClientTest, AutoDeauth_SwitchingChannelBeforeDeauthTimeoutCouldTrigger) {
    Connect();

    // No deauth since off channel.
    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout);
    GoOffChannel();
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    IncreaseTimeByBeaconPeriods(1);
    GoBackToMainChannel();

    // Auto-deauth timeout shouldn't trigger yet. This is because after going back on channel,
    // the client should always schedule timeout sufficiently far enough in the future
    // (at least one beacon interval)
    station.HandleTimeout();
    ASSERT_TRUE(device.wlan_queue.empty());

    // Auto-deauth now
    IncreaseTimeByBeaconPeriods(1);
    station.HandleTimeout();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    AssertDeauthFrame(std::move(*device.wlan_queue.begin()),
                      wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);
    auto deauths =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_EQ(deauths.size(), static_cast<size_t>(1));
}

TEST_F(ClientTest, AutoDeauth_ForeignBeaconShouldNotPreventDeauth) {
    Connect();

    IncreaseTimeByBeaconPeriods(kAutoDeauthTimeout - 1);
    SendBeaconFrame(common::MacAddr(kBssid2));  // beacon frame from another AP

    IncreaseTimeByBeaconPeriods(1);
    station.HandleTimeout();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));
    AssertDeauthFrame(std::move(*device.wlan_queue.begin()),
                      wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH);
    auto deauths =
        device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEDeauthenticateIndOrdinal>);
    ASSERT_EQ(deauths.size(), static_cast<size_t>(1));
}

TEST_F(ClientTest, BufferFramesWhileOffChannelAndSendWhenOnChannel) {
    Connect();

    GoOffChannel();
    SendEthFrame();
    ASSERT_TRUE(device.wlan_queue.empty());

    GoBackToMainChannel();
    ASSERT_EQ(device.wlan_queue.size(), static_cast<size_t>(1));

    auto pkt = std::move(*device.wlan_queue.begin());
    ASSERT_EQ(pkt->peer(), Packet::Peer::kWlan);
    auto type_checked_frame = DataFrameView<LlcHeader>::CheckType(pkt.get());
    ASSERT_TRUE(type_checked_frame);
    auto frame = type_checked_frame.CheckLength();
    ASSERT_TRUE(frame);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr1.byte, kBssid1, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr2.byte, kClientAddress, 6), 0);
    ASSERT_EQ(std::memcmp(frame.hdr()->addr3.byte, kBssid1, 6), 0);

    auto llc_hdr = frame.body();
    ASSERT_EQ(frame.body_len() - llc_hdr->len(), sizeof(kTestPayload));
    ASSERT_EQ(std::memcmp(llc_hdr->payload, kTestPayload, sizeof(kTestPayload)), 0);
}

TEST_F(ClientTest, InvalidAuthenticationResponse) {
    Join();

    // Send AUTHENTICATION.request. Verify that no confirmation was sent yet.
    ASSERT_EQ(SendMlmeMsg<wlan_mlme::AuthenticateRequest>(), ZX_OK);
    ASSERT_TRUE(device.svc_queue.empty());

    // Send authentication frame with wrong algorithm.
    fbl::unique_ptr<Packet> auth_pkt;
    ASSERT_EQ(CreateFrame<Authentication>(&auth_pkt), ZX_OK);
    MgmtFrame<Authentication> auth_frame(fbl::move(auth_pkt));
    auth_frame.body()->auth_algorithm_number = AuthAlgorithm::kSae;
    ASSERT_EQ(station.HandleAnyWlanFrame(auth_frame.Take()), ZX_OK);

    // Verify that AUTHENTICATION.confirm was received.
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto auths = device.GetServicePackets(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateConfOrdinal>);
    ASSERT_FALSE(auths.empty());
    AssertAuthConfirm(fbl::move(*auths.begin()),
                      wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);

    // Fast forward in time would have caused a timeout.
    // The timeout however should have been canceled and we should not receive
    // and additional confirmation.
    SetTimeInBeaconPeriods(kAuthTimeout);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.empty());

    // Send a second, now valid authentication frame.
    // This frame should be ignored as the client reset.
    ASSERT_EQ(CreateFrame<Authentication>(&auth_pkt), ZX_OK);
    auth_frame = MgmtFrame<Authentication>(fbl::move(auth_pkt));
    ASSERT_EQ(station.HandleAnyWlanFrame(auth_frame.Take()), ZX_OK);

    // Fast forward in time far beyond an authentication timeout.
    // There should not be any AUTHENTICATION.confirm sent as the client
    // is expected to have been reset into a |joined| state after failing
    // to authenticate.
    SetTimeInBeaconPeriods(1000);
    station.HandleTimeout();
    ASSERT_TRUE(device.svc_queue.empty());
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
