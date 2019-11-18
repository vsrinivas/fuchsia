// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/wlan.h>

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
  fidl_msg->reason_code = wlan_mlme::ReasonCode::UNSPECIFIED_REASON;
  common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.data());

  fidl::Encoder enc(42);
  SerializeServiceMsg(&enc, fidl_msg.get());

  // Verify correctness.
  auto mlme_msg = MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(enc.GetMessage().bytes(), 0);
  ASSERT_TRUE(mlme_msg.has_value());
  auto deauth_conf = mlme_msg->body();
  ASSERT_NE(deauth_conf, nullptr);
  ASSERT_EQ(memcmp(deauth_conf->peer_sta_address.data(), common::kBcastMac.byte, 6), 0);
}

TEST(MlmeMsg, Generalize) {
  // Construct simple message and write it to a Packet.
  auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
  fidl_msg->reason_code = wlan_mlme::ReasonCode::UNSPECIFIED_REASON;
  common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.data());
  fidl::Encoder enc(42);
  SerializeServiceMsg(&enc, fidl_msg.get());

  auto mlme_msg = MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(enc.GetMessage().bytes(), 0);
  ASSERT_TRUE(mlme_msg.has_value());

  // Generalize message and attempt to specialize to wrong type.
  auto& generic_mlme_msg = static_cast<BaseMlmeMsg&>(*mlme_msg);
  ASSERT_EQ(generic_mlme_msg.As<wlan_mlme::ScanRequest>(), nullptr);

  // Specialize message to correct type.
  auto deauth_conf = generic_mlme_msg.As<wlan_mlme::DeauthenticateRequest>();
  ASSERT_NE(deauth_conf, nullptr);
  ASSERT_EQ(memcmp(deauth_conf->body()->peer_sta_address.data(), common::kBcastMac.byte, 6), 0);
}

TEST(MlmeMsg, CorruptedPacket) {
  // Construct simple message but shorten it.
  auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
  fidl_msg->reason_code = wlan_mlme::ReasonCode::UNSPECIFIED_REASON;
  common::kBcastMac.CopyTo(fidl_msg->peer_sta_address.data());
  fidl::Encoder enc(42);
  SerializeServiceMsg(&enc, fidl_msg.get());
  fbl::Span<uint8_t> span(enc.GetMessage().bytes());
  fbl::Span<uint8_t> invalid_span(span.data(), span.size() - 1);

  // Verify correctness.
  auto mlme_msg = MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(invalid_span, 0);
  ASSERT_FALSE(mlme_msg.has_value());
}

TEST(MlmeMsg, MismatchingOrdinal) {
  auto fidl_msg = wlan_mlme::DeauthenticateRequest::New();
  fidl_msg->reason_code = wlan_mlme::ReasonCode::UNSPECIFIED_REASON;
  fidl::Encoder enc(42);
  SerializeServiceMsg(&enc, fidl_msg.get());

  // Type is correct but ordinal does not match
  auto mlme_msg = MlmeMsg<wlan_mlme::DeauthenticateRequest>::Decode(
      enc.GetMessage().bytes(), fuchsia::wlan::mlme::internal::kMLME_DeauthenticateInd_GenOrdinal);
  ASSERT_FALSE(mlme_msg.has_value());
}

TEST_F(ServiceTest, SendAuthInd) {
  const common::MacAddr peer_sta({0x48, 0x0f, 0xcf, 0x54, 0xb9, 0xb1});
  wlan_mlme::AuthenticationTypes auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;

  service::SendAuthIndication(&device, peer_sta, auth_type);

  ASSERT_EQ(device.svc_queue.size(), static_cast<size_t>(1));
  auto msgs = device.GetServiceMsgs<wlan_mlme::AuthenticateIndication>(
      fuchsia::wlan::mlme::internal::kMLME_AuthenticateInd_GenOrdinal);
  ASSERT_EQ(msgs.size(), 1ULL);

  ASSERT_EQ(std::memcmp(msgs[0].body()->peer_sta_address.data(), peer_sta.byte, 6), 0);
  ASSERT_EQ(msgs[0].body()->auth_type, wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
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
  auto msgs = device.GetServiceMsgs<wlan_mlme::AssociateIndication>(
      fuchsia::wlan::mlme::internal::kMLME_AssociateInd_GenOrdinal);
  ASSERT_EQ(msgs.size(), 1ULL);

  ASSERT_EQ(std::memcmp(msgs[0].body()->peer_sta_address.data(), peer_sta.byte, 6), 0);
  ASSERT_EQ(msgs[0].body()->listen_interval, 100);
  ASSERT_TRUE(std::equal(msgs[0].body()->ssid->begin(), msgs[0].body()->ssid->end(),
                         std::begin(ssid), std::end(ssid)));
  ASSERT_EQ(std::memcmp(msgs[0].body()->rsne->data(), expected_rsne, sizeof(expected_rsne)), 0);
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
  auto msgs = device.GetServiceMsgs<wlan_mlme::AssociateIndication>(
      fuchsia::wlan::mlme::internal::kMLME_AssociateInd_GenOrdinal);
  ASSERT_EQ(msgs.size(), 1ULL);

  ASSERT_EQ(std::memcmp(msgs[0].body()->peer_sta_address.data(), peer_sta.byte, 6), 0);
  ASSERT_EQ(msgs[0].body()->listen_interval, 100);
  ASSERT_TRUE(std::equal(msgs[0].body()->ssid->begin(), msgs[0].body()->ssid->end(),
                         std::begin(ssid), std::end(ssid)));
  ASSERT_FALSE(msgs[0].body()->rsne.has_value());
}

}  // namespace
}  // namespace wlan
