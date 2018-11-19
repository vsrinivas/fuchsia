// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/timekeeper/clock.h>
#include <wlan/common/macaddr.h>
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

#include <gtest/gtest.h>

namespace wlan {

// TODO(hahnr): Extract into a configuration struct which is passed to frame construction.
// This allows to easily switch between different BSS to join to.
static constexpr uint8_t kBssid1[6] = {0xB7, 0xCD, 0x3F, 0xB0, 0x93, 0x01};
static constexpr uint8_t kBssid2[6] = {0xAC, 0xBF, 0x34, 0x11, 0x95, 0x02};
static constexpr uint32_t kJoinTimeout = 200;        // Beacon Periods
static constexpr uint32_t kAuthTimeout = 200;        // Beacon Periods
static constexpr uint32_t kAutoDeauthTimeout = 100;  // Beacon Periods
static constexpr uint16_t kAid = 2;
static constexpr uint16_t kBeaconPeriodTu = 100;
static constexpr uint16_t kDtimPeriodTu = 2;
static constexpr uint8_t kListenInterval = 10;  // Beacon Periods
static constexpr wlan_channel_t kBssChannel = {
    .cbw = CBW20,
    .primary = 11,
};
static constexpr PHY kBssPhy = PHY::WLAN_PHY_OFDM;
static constexpr uint8_t kSsid[] = {'F', 'u', 'c', 'h', 's', 'i', 'a', '-', 'A', 'P'};
static constexpr uint8_t kEapolPdu[] = {'E', 'A', 'P', 'O', 'L'};
static constexpr uint8_t kKeyData[] = {0x40, 0x41, 0x42, 0x43, 0x44};
static constexpr SupportedRate kSupportedRates[] = {
    SupportedRate(2),  SupportedRate(12), SupportedRate(24), SupportedRate(48),
    SupportedRate(54), SupportedRate(96), SupportedRate(108),
    SupportedRate(1),  SupportedRate(16), SupportedRate(36)};

static constexpr uint8_t kRsne[] = {
    0x30,                    // element id
    0x12,                    // length
    0x00, 0x0f, 0xac, 0x04,  // group data cipher suite
    0x01, 0x00,              // pairwise cipher suite count
    0x00, 0x0f, 0xac, 0x04,  // pairwise cipher suite list
    0x01, 0x00,              // akm suite count
    0x00, 0x0f, 0xac, 0x02,  // akm suite list
    0xa8, 0x04,              // rsn capabilities
};
static constexpr uint8_t kCipherOui[3] = {0x96, 0x85, 0x74};
static constexpr uint8_t kCipherSuiteType = 0x11;

::fuchsia::wlan::mlme::BSSDescription CreateBssDescription();
zx_status_t CreateStartRequest(MlmeMsg<::fuchsia::wlan::mlme::StartRequest>*, bool protected_ap);
zx_status_t CreateJoinRequest(MlmeMsg<::fuchsia::wlan::mlme::JoinRequest>*);
zx_status_t CreateAuthRequest(MlmeMsg<::fuchsia::wlan::mlme::AuthenticateRequest>*);
zx_status_t CreateAuthResponse(MlmeMsg<::fuchsia::wlan::mlme::AuthenticateResponse>*,
                               common::MacAddr client_addr,
                               ::fuchsia::wlan::mlme::AuthenticateResultCodes result_code);
zx_status_t CreateDeauthRequest(MlmeMsg<::fuchsia::wlan::mlme::DeauthenticateRequest>*,
                                common::MacAddr, ::fuchsia::wlan::mlme::ReasonCode reason_code);
zx_status_t CreateAssocRequest(MlmeMsg<::fuchsia::wlan::mlme::AssociateRequest>* out_msg);
zx_status_t CreateAssocResponse(MlmeMsg<::fuchsia::wlan::mlme::AssociateResponse>*,
                                common::MacAddr client_addr,
                                ::fuchsia::wlan::mlme::AssociateResultCodes result_code,
                                uint16_t aid);
zx_status_t CreateEapolRequest(MlmeMsg<::fuchsia::wlan::mlme::EapolRequest>*,
                               common::MacAddr client_addr);
zx_status_t CreateSetKeysRequest(MlmeMsg<::fuchsia::wlan::mlme::SetKeysRequest>*,
                                 common::MacAddr client_addr, std::vector<uint8_t> key_data,
                                 ::fuchsia::wlan::mlme::KeyType);
zx_status_t CreateAuthReqFrame(fbl::unique_ptr<Packet>*, common::MacAddr client_addr);
zx_status_t CreateAuthRespFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateDeauthFrame(fbl::unique_ptr<Packet>*, common::MacAddr client_addr);
zx_status_t CreateBeaconFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateBeaconFrameWithBssid(fbl::unique_ptr<Packet>*, common::MacAddr);
zx_status_t CreateProbeRequest(fbl::unique_ptr<Packet>*);
zx_status_t CreateAssocReqFrame(fbl::unique_ptr<Packet>*, common::MacAddr client_addr,
                                Span<const uint8_t> ssid, bool rsn);
zx_status_t CreateAssocRespFrame(fbl::unique_ptr<Packet>*);
zx_status_t CreateDisassocFrame(fbl::unique_ptr<Packet>*, common::MacAddr client_addr);
DataFrame<LlcHeader> CreateDataFrame(const uint8_t* payload, size_t len);
DataFrame<> CreateNullDataFrame();
EthFrame CreateEthFrame(const uint8_t* payload, size_t len);

template <typename F> zx_status_t CreateFrame(fbl::unique_ptr<Packet>* pkt) {
    if (std::is_same<F, Authentication>::value) { return CreateAuthRespFrame(pkt); }
    if (std::is_same<F, AssociationResponse>::value) { return CreateAssocRespFrame(pkt); }
    if (std::is_same<F, Beacon>::value) { return CreateBeaconFrame(pkt); }

    return ZX_ERR_NOT_SUPPORTED;
}

template <typename M, typename E>
using enable_if_same = std::enable_if_t<std::is_same<M, E>::value, zx_status_t>;

template <typename M>
enable_if_same<M, ::fuchsia::wlan::mlme::AuthenticateRequest> CreateMlmeMsg(MlmeMsg<M>* msg) {
    return CreateAuthRequest(msg);
}

template <typename M>
enable_if_same<M, ::fuchsia::wlan::mlme::JoinRequest> CreateMlmeMsg(MlmeMsg<M>* msg) {
    return CreateJoinRequest(msg);
}

template <typename M>
enable_if_same<M, ::fuchsia::wlan::mlme::AssociateRequest> CreateMlmeMsg(MlmeMsg<M>* msg) {
    return CreateAssocRequest(msg);
}

template <typename T>
static zx_status_t WriteServiceMessage(T* message, uint32_t ordinal, MlmeMsg<T>* out_msg) {
    auto packet = GetSvcPacket(16384);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    zx_status_t status = SerializeServiceMsg(packet.get(), ordinal, message);
    if (status != ZX_OK) { return status; }

    return MlmeMsg<T>::FromPacket(std::move(packet), out_msg);
}

}  // namespace wlan
