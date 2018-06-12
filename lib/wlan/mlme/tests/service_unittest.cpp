// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/service.h>
#include <wlan/mlme/wlan.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace {

template <typename T>
static fbl::unique_ptr<Packet> IntoPacket(const T& msg, uint32_t ordinal = 42) {
    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    constexpr size_t kBufLen = 4096;

    auto buffer = GetBuffer(kBufLen);
    memset(buffer->data(), 0, kBufLen);
    auto pkt = fbl::make_unique<Packet>(fbl::move(buffer), kBufLen);
    pkt->set_peer(Packet::Peer::kService);
    SerializeServiceMsg(pkt.get(), ordinal, msg.get());
    return fbl::move(pkt);
}

TEST(MlmeMsg, General) {
    // Construct simple message and write it to a Packet.
    auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
    common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.mutable_data());
    auto pkt = IntoPacket(fidl_msg);

    // Verify correctness.
    MlmeMsg<wlan_mlme::DeauthenticateRequest> mlme_msg;
    auto status = MlmeMsg<wlan_mlme::DeauthenticateRequest>::FromPacket(fbl::move(pkt), &mlme_msg);
    ASSERT_EQ(status, ZX_OK);
    auto deauth_conf = mlme_msg.body();
    ASSERT_NE(deauth_conf, nullptr);
    ASSERT_EQ(memcmp(deauth_conf->peer_sta_address.data(), common::kBcastMac.byte, 6), 0);
}

TEST(MlmeMsg, Generalize) {
    // Construct simple message and write it to a Packet.
    auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
    common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.mutable_data());
    auto pkt = IntoPacket(fidl_msg);

    MlmeMsg<wlan_mlme::DeauthenticateRequest> mlme_msg;
    auto status = MlmeMsg<wlan_mlme::DeauthenticateRequest>::FromPacket(fbl::move(pkt), &mlme_msg);
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
    auto pkt = IntoPacket(fidl_msg);
    pkt->set_len(pkt->len() - 1);

    // Verify correctness.
    MlmeMsg<wlan_mlme::DeauthenticateRequest> mlme_msg;
    auto status = MlmeMsg<wlan_mlme::DeauthenticateRequest>::FromPacket(fbl::move(pkt), &mlme_msg);
    ASSERT_NE(status, ZX_OK);
}

}  // namespace
}  // namespace wlan
