// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/service.h>
#include <wlan/mlme/wlan.h>

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "mock_device.h"

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace {

struct ServiceTest : public ::testing::Test {
    ServiceTest() : device() {}

    MockDevice device;
};

TEST(MlmeMsg, General) {
    // Construct simple message and write it to a Packet.
    auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
    common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.mutable_data());

    fidl::Encoder enc(42);
    SerializeServiceMsg(&enc, fidl_msg.get());

    // Verify correctness.
    MlmeMsg<wlan_mlme::DeauthenticateRequest> mlme_msg;
    auto status =
        MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(enc.GetMessage().bytes(), &mlme_msg);
    ASSERT_EQ(status, ZX_OK);
    auto deauth_conf = mlme_msg.body();
    ASSERT_NE(deauth_conf, nullptr);
    ASSERT_EQ(memcmp(deauth_conf->peer_sta_address.data(), common::kBcastMac.byte, 6), 0);
}

TEST(MlmeMsg, Generalize) {
    // Construct simple message and write it to a Packet.
    auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
    common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.mutable_data());
    fidl::Encoder enc(42);
    SerializeServiceMsg(&enc, fidl_msg.get());

    MlmeMsg<wlan_mlme::DeauthenticateRequest> mlme_msg;
    auto status =
        MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(enc.GetMessage().bytes(), &mlme_msg);
    ASSERT_EQ(status, ZX_OK);

    // Generalize message and attempt to specialize to wrong type.
    auto& generic_mlme_msg = static_cast<BaseMlmeMsg&>(mlme_msg);
    ASSERT_EQ(generic_mlme_msg.As<wlan_mlme::ScanRequest>(), nullptr);

    // Specialize message to correct type.
    auto deauth_conf = generic_mlme_msg.As<wlan_mlme::DeauthenticateRequest>();
    ASSERT_NE(deauth_conf, nullptr);
    ASSERT_EQ(memcmp(deauth_conf->body()->peer_sta_address.data(), common::kBcastMac.byte, 6), 0);
}

TEST(MlmeMsg, CorruptedPacket) {
    // Construct simple message but shorten it.
    auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
    common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.mutable_data());
    fidl::Encoder enc(42);
    SerializeServiceMsg(&enc, fidl_msg.get());
    Span<uint8_t> span(enc.GetMessage().bytes());
    Span<uint8_t> invalid_span(span.data(), span.size() - 1);

    // Verify correctness.
    MlmeMsg<wlan_mlme::DeauthenticateRequest> mlme_msg;
    auto status = MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(invalid_span, &mlme_msg);
    ASSERT_NE(status, ZX_OK);
}

TEST_F(ServiceTest, SendAuthInd) {
    const common::MacAddr peer_sta({0x48, 0x0f, 0xcf, 0x54, 0xb9, 0xb1});
    wlan_mlme::AuthenticationTypes auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;

    service::SendAuthIndication(&device, peer_sta, auth_type);

    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto inds = device.GetServiceMsgs(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAuthenticateIndOrdinal>);
    ASSERT_EQ(inds.size(), static_cast<size_t>(1));
    MlmeMsg<wlan_mlme::AuthenticateIndication> msg;
    auto status = MlmeMsg<wlan_mlme::AuthenticateIndication>::Decode(inds[0], &msg);
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), peer_sta.byte, 6), 0);
    ASSERT_EQ(msg.body()->auth_type, wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
}

TEST_F(ServiceTest, SendAssocInd) {
    // -- prepare
    const common::MacAddr peer_sta({0x48, 0x0f, 0xcf, 0x54, 0xb9, 0xb1});
    uint16_t listen_interval = 100;

    constexpr uint8_t ssid[] = {'F', 'U', 'C', 'H', 'S', 'I', 'A'};
    constexpr uint8_t rsne_body[] = {1, 2, 3, 4, 5, 6, 7, 8};
    constexpr uint8_t expected_rsne[] = {0x30, 8u, 1, 2, 3, 4, 5, 6, 7, 8};

    // -- execute
    service::SendAssocIndication(&device, peer_sta, listen_interval, ssid, {{rsne_body}});

    // -- verify
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto inds = device.GetServiceMsgs(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_EQ(inds.size(), static_cast<size_t>(1));
    MlmeMsg<wlan_mlme::AssociateIndication> msg;
    auto status = MlmeMsg<wlan_mlme::AssociateIndication>::Decode(inds[0], &msg);
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), peer_sta.byte, 6), 0);
    ASSERT_EQ(msg.body()->listen_interval, 100);
    ASSERT_TRUE(std::equal(msg.body()->ssid->begin(), msg.body()->ssid->end(), std::begin(ssid),
                           std::end(ssid)));
    ASSERT_EQ(std::memcmp(msg.body()->rsn->data(), expected_rsne, sizeof(expected_rsne)), 0);
}

TEST_F(ServiceTest, SendAssocInd_EmptyRsne) {
    // -- prepare
    const common::MacAddr peer_sta({0x48, 0x0f, 0xcf, 0x54, 0xb9, 0xb1});
    uint16_t listen_interval = 100;

    constexpr uint8_t ssid[] = {'F', 'U', 'C', 'H', 'S', 'I', 'A'};
    // -- execute
    service::SendAssocIndication(&device, peer_sta, listen_interval, ssid, {});

    // -- verify
    ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
    auto inds = device.GetServiceMsgs(IsMlmeMsg<fuchsia_wlan_mlme_MLMEAssociateIndOrdinal>);
    ASSERT_EQ(inds.size(), static_cast<size_t>(1));
    MlmeMsg<wlan_mlme::AssociateIndication> msg;
    auto status = MlmeMsg<wlan_mlme::AssociateIndication>::Decode(inds[0], &msg);
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(std::memcmp(msg.body()->peer_sta_address.data(), peer_sta.byte, 6), 0);
    ASSERT_EQ(msg.body()->listen_interval, 100);
    ASSERT_TRUE(std::equal(msg.body()->ssid->begin(), msg.body()->ssid->end(), std::begin(ssid),
                           std::end(ssid)));
    ASSERT_TRUE(msg.body()->rsn.is_null());
}

}  // namespace
}  // namespace wlan
