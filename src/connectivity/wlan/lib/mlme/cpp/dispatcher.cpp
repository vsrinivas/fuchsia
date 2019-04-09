// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/dispatcher.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <fuchsia/wlan/minstrel/cpp/fidl.h>
#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <zircon/fidl.h>
#include <atomic>
#include <cinttypes>
#include <cstring>
#include <sstream>

#include "lib/svc/cpp/services.h"

namespace wlan {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_minstrel = ::fuchsia::wlan::minstrel;
namespace wlan_stats = ::fuchsia::wlan::stats;

Dispatcher::Dispatcher(DeviceInterface* device, fbl::unique_ptr<Mlme> mlme)
    : device_(device), mlme_(std::move(mlme)) {
    debugfn();
    ZX_ASSERT(mlme_ != nullptr);
}

Dispatcher::~Dispatcher() {}

zx_status_t Dispatcher::HandlePacket(fbl::unique_ptr<Packet> packet) {
    debugfn();

    ZX_DEBUG_ASSERT(packet != nullptr);
    ZX_DEBUG_ASSERT(packet->peer() != Packet::Peer::kUnknown);

    finspect("Packet: %s\n", debug::Describe(*packet).c_str());

    WLAN_STATS_INC(any_packet.in);

    // If there is no active MLME, block all packets but service ones.
    // MLME-JOIN.request and MLME-START.request implicitly select a mode and initialize the
    // MLME. DEVICE_QUERY.request is used to obtain device capabilities.

    auto service_msg = (packet->peer() == Packet::Peer::kService);
    if (mlme_ == nullptr && !service_msg) {
        WLAN_STATS_INC(any_packet.drop);
        return ZX_OK;
    }

    WLAN_STATS_INC(any_packet.out);

    zx_status_t status = ZX_OK;
    switch (packet->peer()) {
    case Packet::Peer::kEthernet:
        status = mlme_->HandleFramePacket(std::move(packet));
        break;
    case Packet::Peer::kWlan:
        if (auto fc = packet->field<FrameControl>(0)) {
            switch (fc->type()) {
            case FrameType::kManagement:
                WLAN_STATS_INC(mgmt_frame.in);
                break;
            case FrameType::kControl:
                WLAN_STATS_INC(ctrl_frame.in);
                break;
            case FrameType::kData:
                WLAN_STATS_INC(data_frame.in);
                break;
            default:
                break;
            }

            status = mlme_->HandleFramePacket(std::move(packet));
        }
        break;
    default:
        break;
    }

    return status;
}

zx_status_t Dispatcher::HandlePortPacket(uint64_t key) {
    debugfn();
    ZX_DEBUG_ASSERT(ToPortKeyType(key) == PortKeyType::kMlme);

    ObjectId id(ToPortKeyId(key));
    switch (id.subtype()) {
    case to_enum_type(ObjectSubtype::kTimer): {
        auto status = mlme_->HandleTimeout(id);
        if (status == ZX_ERR_NOT_SUPPORTED) {
            warnf("unknown MLME timer target: %u\n", id.target());
        }
        break;
    }
    default:
        warnf("unknown MLME event subtype: %u\n", id.subtype());
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleAnyMlmeMessage(Span<uint8_t> span) {
    debugfn();

    auto hdr = FromBytes<fidl_message_header_t>(span);
    if (hdr == nullptr) {
        errorf("short mlme message, len=%zu\n", span.size());
        return ZX_OK;
    }
    debughdr("service packet txid=%u flags=%u ordinal=%u\n", hdr->txid, hdr->flags, hdr->ordinal);

    // Messages in wlan_mlme_ext.fidl does not involve MLME.
    switch (hdr->ordinal) {
    case fuchsia_wlan_mlme_MLMEQueryDeviceInfoOrdinal:
        return HandleQueryDeviceInfo(hdr->txid);
    case fuchsia_wlan_mlme_MLMEStatsQueryReqOrdinal:
        return HandleMlmeStats(hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEListMinstrelPeersOrdinal:
        return HandleMinstrelPeerList(hdr->ordinal, hdr->txid);
    case fuchsia_wlan_mlme_MLMEGetMinstrelStatsOrdinal:
        return HandleMinstrelTxStats(span, hdr->ordinal, hdr->txid);
    }

    switch (hdr->ordinal) {
    case fuchsia_wlan_mlme_MLMEResetReqOrdinal:
        infof("resetting MLME\n");
        HandleMlmeMessage<wlan_mlme::ResetRequest>(span, hdr->ordinal);
        return ZX_OK;
    case fuchsia_wlan_mlme_MLMEStartReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::StartRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEStopReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::StopRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEStartScanOrdinal:
        return HandleMlmeMessage<wlan_mlme::ScanRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEJoinReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::JoinRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAuthenticateReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::AuthenticateRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAuthenticateRespOrdinal:
        return HandleMlmeMessage<wlan_mlme::AuthenticateResponse>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEDeauthenticateReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::DeauthenticateRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAssociateReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::AssociateRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAssociateRespOrdinal:
        return HandleMlmeMessage<wlan_mlme::AssociateResponse>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEEapolReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::EapolRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMESetKeysReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::SetKeysRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMESetControlledPortOrdinal:
        return HandleMlmeMessage<wlan_mlme::SetControlledPortRequest>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMESendMpOpenActionOrdinal:
        return HandleMlmeMessage<wlan_mlme::MeshPeeringOpenAction>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMESendMpConfirmActionOrdinal:
        return HandleMlmeMessage<wlan_mlme::MeshPeeringConfirmAction>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEMeshPeeringEstablishedOrdinal:
        return HandleMlmeMessage<wlan_mlme::MeshPeeringParams>(span, hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEGetMeshPathTableReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::GetMeshPathTableRequest>(span, hdr->ordinal);
    default:
        warnf("unknown MLME method %u\n", hdr->ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

template <typename Message>
zx_status_t Dispatcher::HandleMlmeMessage(Span<uint8_t> span, uint32_t ordinal) {
    auto msg = MlmeMsg<Message>::Decode(span, ordinal);
    if (!msg.has_value()) {
        errorf("could not deserialize MLME primitive %d: \n", ordinal);
        return ZX_ERR_INVALID_ARGS;
    }
    return mlme_->HandleMlmeMsg(*msg);
}

zx_status_t Dispatcher::HandleQueryDeviceInfo(zx_txid_t txid) {
    debugfn();

    wlan_mlme::DeviceInfo resp;
    const wlan_info_t& info = device_->GetWlanInfo().ifc_info;

    memcpy(resp.mac_addr.data(), info.mac_addr, ETH_MAC_SIZE);

    // mac_role is a bitfield, but only a single value is supported for an interface
    switch (info.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
        resp.role = wlan_mlme::MacRole::CLIENT;
        break;
    case WLAN_MAC_ROLE_AP:
        resp.role = wlan_mlme::MacRole::AP;
        break;
    case WLAN_MAC_ROLE_MESH:
        resp.role = wlan_mlme::MacRole::MESH;
        break;
    default:
        // TODO(NET-1116): return an error!
        break;
    }

    auto wlanmac_info = device_->GetWlanInfo().ifc_info;

    resp.bands.resize(0);
    for (uint8_t band_idx = 0; band_idx < info.num_bands; band_idx++) {
        const wlan_band_info_t& band_info = info.bands[band_idx];
        wlan_mlme::BandCapabilities band;
        band.band_id = wlan::common::BandToFidl(band_info.band_id);
        band.basic_rates.resize(0);
        for (size_t rate_idx = 0; rate_idx < sizeof(band_info.basic_rates); rate_idx++) {
            if (band_info.basic_rates[rate_idx] != 0) {
                band.basic_rates.push_back(band_info.basic_rates[rate_idx]);
            }
        }
        const wlan_chan_list_t& chan_list = band_info.supported_channels;
        band.base_frequency = chan_list.base_freq;
        band.channels.resize(0);
        for (size_t chan_idx = 0; chan_idx < sizeof(chan_list.channels); chan_idx++) {
            if (chan_list.channels[chan_idx] != 0) {
                band.channels.push_back(chan_list.channels[chan_idx]);
            }
        }

        band.cap = CapabilityInfo::FromDdk(wlanmac_info.caps).ToFidl();

        if (band_info.ht_supported) {
            auto ht_cap = HtCapabilities::FromDdk(band_info.ht_caps);
            band.ht_cap = std::make_unique<wlan_mlme::HtCapabilities>(ht_cap.ToFidl());
        }
        if (band_info.vht_supported) {
            auto vht_cap = VhtCapabilities::FromDdk(band_info.vht_caps);
            band.vht_cap = std::make_unique<wlan_mlme::VhtCapabilities>(vht_cap.ToFidl());
        }

        resp.bands.push_back(std::move(band));
    }

    resp.driver_features.resize(0);
    if (info.driver_features & WLAN_DRIVER_FEATURE_SCAN_OFFLOAD) {
        resp.driver_features.push_back(wlan_common::DriverFeature::SCAN_OFFLOAD);
    }
    if (info.driver_features & WLAN_DRIVER_FEATURE_RATE_SELECTION) {
        resp.driver_features.push_back(wlan_common::DriverFeature::RATE_SELECTION);
    }
    if (info.driver_features & WLAN_DRIVER_FEATURE_SYNTH) {
        resp.driver_features.push_back(wlan_common::DriverFeature::SYNTH);
    }
    if (info.driver_features & WLAN_DRIVER_FEATURE_TX_STATUS_REPORT) {
        resp.driver_features.push_back(wlan_common::DriverFeature::TX_STATUS_REPORT);
    }
    if (info.driver_features & WLAN_DRIVER_FEATURE_DFS) {
        resp.driver_features.push_back(wlan_common::DriverFeature::DFS);
    }

    return SendServiceMsg(device_, &resp, fuchsia_wlan_mlme_MLMEQueryDeviceInfoOrdinal, txid);
}

zx_status_t Dispatcher::HandleMlmeStats(uint32_t ordinal) const {
    debugfn();
    wlan_mlme::StatsQueryResponse resp = GetStatsToFidl();
    return SendServiceMsg(device_, &resp, fuchsia_wlan_mlme_MLMEStatsQueryRespOrdinal);
}

zx_status_t Dispatcher::HandleMinstrelPeerList(uint32_t ordinal, zx_txid_t txid) const {
    debugfn();
    wlan_mlme::MinstrelListResponse resp{};
    zx_status_t status = device_->GetMinstrelPeers(&resp.peers);
    if (status != ZX_OK) {
        errorf("cannot get minstrel peer list: %s\n", zx_status_get_string(status));
        resp.peers.peers.resize(0);
    }
    return SendServiceMsg(device_, &resp, fuchsia_wlan_mlme_MLMEListMinstrelPeersOrdinal, txid);
}

zx_status_t Dispatcher::HandleMinstrelTxStats(Span<uint8_t> span, uint32_t ordinal,
                                              zx_txid_t txid) const {
    debugfn();
    wlan_mlme::MinstrelStatsResponse resp{};
    auto req = MlmeMsg<wlan_mlme::MinstrelStatsRequest>::Decode(
        span, fuchsia_wlan_mlme_MLMEGetMinstrelStatsOrdinal);
    if (!req.has_value()) {
        errorf("could not deserialize MLME primitive %d\n", ordinal);
        return ZX_ERR_INVALID_ARGS;
    }
    common::MacAddr addr(req->body()->mac_addr);

    wlan_minstrel::Peer peer;
    auto status = device_->GetMinstrelStats(addr, &peer);
    if (status == ZX_OK) {
        resp.peer = std::make_unique<wlan_minstrel::Peer>(std::move(peer));
    } else {
        errorf("could not get peer stats: %s\n", zx_status_get_string(status));
    }
    return SendServiceMsg(device_, &resp, fuchsia_wlan_mlme_MLMEGetMinstrelStatsOrdinal, txid);
}

void Dispatcher::HwIndication(uint32_t ind) {
    debugfn();
    mlme_->HwIndication(ind);
}

void Dispatcher::HwScanComplete(uint8_t result_code) {
    debugfn();
    mlme_->HwScanComplete(result_code);
}

void Dispatcher::ResetStats() {
    stats_.Reset();
    if (mlme_) { mlme_->ResetMlmeStats(); }
}

wlan_mlme::StatsQueryResponse Dispatcher::GetStatsToFidl() const {
    wlan_mlme::StatsQueryResponse stats_response;
    stats_response.stats.dispatcher_stats = stats_.ToFidl();
    auto mlme_stats = mlme_->GetMlmeStats();
    if (!mlme_stats.has_invalid_tag()) {
        stats_response.stats.mlme_stats =
            std::make_unique<wlan_stats::MlmeStats>(mlme_->GetMlmeStats());
    }
    return stats_response;
}

}  // namespace wlan
