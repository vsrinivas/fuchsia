// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_TEST_BSS_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_TEST_BSS_H_

#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/timekeeper/clock.h>

#include <memory>

#include <ddk/hw/wlan/wlaninfo.h>
#include <gtest/gtest.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include "mock_device.h"

namespace wlan {

// TODO(hahnr): Extract into a configuration struct which is passed to frame
// construction. This allows to easily switch between different BSS to join to.
static constexpr uint8_t kBssid1[6] = {0xB7, 0xCD, 0x3F, 0xB0, 0x93, 0x01};
static constexpr uint8_t kBssid2[6] = {0xAC, 0xBF, 0x34, 0x11, 0x95, 0x02};
static constexpr uint8_t kBroadcastBssid[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static constexpr uint32_t kJoinTimeout = 200;        // Beacon Periods
static constexpr uint32_t kAuthTimeout = 200;        // Beacon Periods
static constexpr uint32_t kAutoDeauthTimeout = 100;  // Beacon Periods
static constexpr uint16_t kAid = 2;
static constexpr uint16_t kBeaconPeriodTu = 100;
static constexpr uint16_t kDtimPeriodTu = 2;
static constexpr uint8_t kListenInterval = 10;  // Beacon Periods
static constexpr wlan_channel_t kBssChannel = {
    .primary = 36,
    .cbw = WLAN_CHANNEL_BANDWIDTH__40,
};
static constexpr wlan_info_phy_type_t kBssPhy = WLAN_INFO_PHY_TYPE_HT;
static constexpr uint8_t kSsid[] = {'F', 'u', 'c', 'h', 's', 'i', 'a', '-', 'A', 'P'};
static constexpr uint8_t kEapolPdu[] = {'E', 'A', 'P', 'O', 'L'};
static constexpr uint8_t kKeyData[] = {0x40, 0x41, 0x42, 0x43, 0x44};
static constexpr SupportedRate kSupportedRates[] = {
    SupportedRate(2),  SupportedRate(12),  SupportedRate(24), SupportedRate(48), SupportedRate(54),
    SupportedRate(96), SupportedRate(108), SupportedRate(1),  SupportedRate(16), SupportedRate(36)};

static constexpr uint8_t kIes[] = {
    // SSID
    0x00, 0x0a, 'F', 'u', 'c', 'h', 's', 'i', 'a', '-', 'A', 'P',
    // Supported rates
    0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
    // DS parameter set - channel 157
    0x03, 0x01, 0x9d,
    // DTIM
    0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
    // Power constraint
    0x20, 0x01, 0x03,
    // HT capabilities
    0x2d, 0x1a, 0xef, 0x09, 0x1b, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // HT operation
    0x3d, 0x16, 0x9d, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Overlapping BSS scan parameters
    0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
    // Extended capabilities
    0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
    // VHT capabilities
    0xbf, 0x0c, 0xb2, 0x01, 0x80, 0x33, 0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00,
    // VHT operation
    0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
    // VHT Tx power envelope
    0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
    // Vendor IE - WMM parameters
    0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4,
    0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00,
    // Vendor IE - Atheros advanced capability
    0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
    // RSN
    0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
    0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
    // Vendor IE - WPS
    0xdd, 0x1d, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01, 0x02,
    0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20};

static constexpr uint8_t kIes_NoRsne[] = {
    // SSID
    0x00, 0x0a, 'F', 'u', 'c', 'h', 's', 'i', 'a', '-', 'A', 'P',
    // Supported rates
    0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
    // DS parameter set - channel 157
    0x03, 0x01, 0x9d,
    // DTIM
    0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
    // Power constraint
    0x20, 0x01, 0x03,
    // HT capabilities
    0x2d, 0x1a, 0xef, 0x09, 0x1b, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // HT operation
    0x3d, 0x16, 0x9d, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Overlapping BSS scan parameters
    0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
    // Extended capabilities
    0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
    // VHT capabilities
    0xbf, 0x0c, 0xb2, 0x01, 0x80, 0x33, 0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00,
    // VHT operation
    0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
    // VHT Tx power envelope
    0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
    // Vendor IE - WMM parameters
    0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4,
    0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00,
    // Vendor IE - Atheros advanced capability
    0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
    // Vendor IE - WPS
    0xdd, 0x1d, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01, 0x02,
    0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20};

static constexpr uint8_t kRsne[] = {
    0x30,                    // element id
    0x14,                    // length
    1,    0,                 // version
    0x00, 0x0f, 0xac, 0x04,  // group data cipher suite
    0x01, 0x00,              // pairwise cipher suite count
    0x00, 0x0f, 0xac, 0x04,  // pairwise cipher suite list
    0x01, 0x00,              // akm suite count
    0x00, 0x0f, 0xac, 0x02,  // akm suite list
    0xa8, 0x04,              // rsn capabilities
};
static constexpr uint8_t kRates[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12,
                                     0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
static constexpr uint8_t kCipherOui[3] = {0x96, 0x85, 0x74};
static constexpr uint8_t kCipherSuiteType = 0x11;
static const wlan_assoc_ctx_t kDdkAssocCtx = {};

template <typename FV>
FV TypeCheckWlanFrame(Packet* pkt) {
  EXPECT_EQ(pkt->peer(), Packet::Peer::kWlan);
  auto type_checked_frame = FV::CheckType(pkt);
  EXPECT_TRUE(type_checked_frame);
  auto frame = type_checked_frame.CheckLength();
  EXPECT_TRUE(frame);
  return frame;
}

::fuchsia::wlan::internal::BssDescription CreateBssDescription(bool rsn,
                                                               wlan_channel_t chan = kBssChannel);
MlmeMsg<::fuchsia::wlan::mlme::ScanRequest> CreateScanRequest(uint32_t max_channel_time);
MlmeMsg<::fuchsia::wlan::mlme::StartRequest> CreateStartRequest(bool protected_ap);
MlmeMsg<::fuchsia::wlan::mlme::StopRequest> CreateStopRequest();
MlmeMsg<::fuchsia::wlan::mlme::JoinRequest> CreateJoinRequest(bool rsn);
MlmeMsg<::fuchsia::wlan::mlme::AuthenticateRequest> CreateAuthRequest();
MlmeMsg<::fuchsia::wlan::mlme::AuthenticateResponse> CreateAuthResponse(
    common::MacAddr client_addr, ::fuchsia::wlan::mlme::AuthenticateResultCodes result_code);
MlmeMsg<::fuchsia::wlan::mlme::DeauthenticateRequest> CreateDeauthRequest(
    common::MacAddr, ::fuchsia::wlan::mlme::ReasonCode reason_code);
MlmeMsg<::fuchsia::wlan::mlme::AssociateRequest> CreateAssocRequest(bool rsn);
MlmeMsg<::fuchsia::wlan::mlme::AssociateResponse> CreateAssocResponse(
    common::MacAddr client_addr, ::fuchsia::wlan::mlme::AssociateResultCodes result_code,
    uint16_t aid);
MlmeMsg<::fuchsia::wlan::mlme::NegotiatedCapabilities> CreateFinalizeAssociationRequest(
    const wlan_assoc_ctx& ac, wlan_channel_t chan);
MlmeMsg<::fuchsia::wlan::mlme::EapolRequest> CreateEapolRequest(common::MacAddr src_addr,
                                                                common::MacAddr dst_addr);
MlmeMsg<::fuchsia::wlan::mlme::SetKeysRequest> CreateSetKeysRequest(common::MacAddr addr,
                                                                    std::vector<uint8_t> key_data,
                                                                    ::fuchsia::wlan::mlme::KeyType);
MlmeMsg<::fuchsia::wlan::mlme::SetControlledPortRequest> CreateSetCtrlPortRequest(
    common::MacAddr peer_addr, ::fuchsia::wlan::mlme::ControlledPortState);
std::unique_ptr<Packet> CreateAuthReqFrame(common::MacAddr client_addr);
std::unique_ptr<Packet> CreateAuthRespFrame(AuthAlgorithm auth_algo);
std::unique_ptr<Packet> CreateDeauthFrame(common::MacAddr client_addr);
std::unique_ptr<Packet> CreateBeaconFrame(common::MacAddr bssid);
std::unique_ptr<Packet> CreateProbeRequest();
std::unique_ptr<Packet> CreateAssocReqFrame(common::MacAddr client_addr,
                                            fbl::Span<const uint8_t> ssid, bool rsn);
std::unique_ptr<Packet> CreateAssocRespFrame(const wlan_assoc_ctx_t& ap_assoc_ctx = kDdkAssocCtx);
std::unique_ptr<Packet> CreateDisassocFrame(common::MacAddr client_addr);
std::unique_ptr<Packet> CreateDataFrame(fbl::Span<const uint8_t> payload);
DataFrame<> CreateNullDataFrame();
std::unique_ptr<Packet> CreateEthFrame(fbl::Span<const uint8_t> payload);
std::unique_ptr<Packet> CreateAmsduDataFramePacket(
    const std::vector<fbl::Span<const uint8_t>>& payloads);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_TEST_BSS_H_
