// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/dispatcher.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/channel.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_dispatcher.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <fuchsia/wlan/mlme/c/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <zircon/fidl.h>
#include <atomic>
#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

constexpr uint8_t kTelemetryReportPeriodMinutes = 1;

namespace wlan_mlme = ::fuchsia::wlan::mlme;
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
    case Packet::Peer::kService:
        status = HandleSvcPacket(fbl::move(packet));
        break;
    case Packet::Peer::kEthernet:
        status = mlme_->HandleFramePacket(fbl::move(packet));
        break;
    case Packet::Peer::kWlan: {
        auto fc = packet->field<FrameControl>(0);
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

        status = mlme_->HandleFramePacket(fbl::move(packet));
        break;
    }
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

zx_status_t Dispatcher::HandleSvcPacket(fbl::unique_ptr<Packet> packet) {
    debugfn();

    const uint8_t* bytes = packet->data();
    auto hdr = FromBytes<fidl_message_header_t>(bytes, packet->len());
    if (hdr == nullptr) {
        errorf("short service packet len=%zu\n", packet->len());
        return ZX_OK;
    }
    debughdr("service packet txid=%u flags=%u ordinal=%u\n", hdr->txid, hdr->flags, hdr->ordinal);

    if (hdr->ordinal == fuchsia_wlan_mlme_MLMEDeviceQueryReqOrdinal) {
        MlmeMsg<wlan_mlme::DeviceQueryRequest> msg;
        auto status = MlmeMsg<wlan_mlme::DeviceQueryRequest>::FromPacket(fbl::move(packet), &msg);
        if (status != ZX_OK) {
            errorf("could not deserialize custom MLME-DeviceQueryRequest primitive\n");
            return status;
        }
        return HandleDeviceQueryRequest();
    }

    if (hdr->ordinal == fuchsia_wlan_mlme_MLMEStatsQueryReqOrdinal) {
        return HandleMlmeStats(hdr->ordinal);
    }

    switch (hdr->ordinal) {
    case fuchsia_wlan_mlme_MLMEResetReqOrdinal:
        infof("resetting MLME\n");
        HandleMlmeMessage<wlan_mlme::ResetRequest>(fbl::move(packet), hdr->ordinal);
        return ZX_OK;
    case fuchsia_wlan_mlme_MLMEStartReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::StartRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEStopReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::StopRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEStartScanOrdinal:
        return HandleMlmeMessage<wlan_mlme::ScanRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEJoinReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::JoinRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAuthenticateReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::AuthenticateRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAuthenticateRespOrdinal:
        return HandleMlmeMessage<wlan_mlme::AuthenticateResponse>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEDeauthenticateReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::DeauthenticateRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAssociateReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::AssociateRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEAssociateRespOrdinal:
        return HandleMlmeMessage<wlan_mlme::AssociateResponse>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMEEapolReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::EapolRequest>(fbl::move(packet), hdr->ordinal);
    case fuchsia_wlan_mlme_MLMESetKeysReqOrdinal:
        return HandleMlmeMessage<wlan_mlme::SetKeysRequest>(fbl::move(packet), hdr->ordinal);
    default:
        warnf("unknown MLME method %u\n", hdr->ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

template <typename Message>
zx_status_t Dispatcher::HandleMlmeMessage(fbl::unique_ptr<Packet> packet, uint32_t ordinal) {
    MlmeMsg<Message> msg;
    auto status = MlmeMsg<Message>::FromPacket(fbl::move(packet), &msg);
    if (status != ZX_OK) {
        errorf("could not deserialize MLME primitive %d: \n", ordinal);
        return status;
    }
    return mlme_->HandleMlmeMsg(msg);
}

template <typename T> zx_status_t Dispatcher::SendServiceMessage(uint32_t ordinal, T* msg) const {
    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    // size_t buf_len = sizeof(fidl_message_header_t) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(std::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), ordinal, msg);
    if (status != ZX_OK) {
        errorf("could not serialize MLME primitive: %d\n", ordinal);
        return status;
    }
    return device_->SendService(fbl::move(packet));
}

zx_status_t Dispatcher::HandleDeviceQueryRequest() {
    debugfn();

    wlan_mlme::DeviceQueryConfirm resp;
    const wlan_info_t& info = device_->GetWlanInfo().ifc_info;

    memcpy(resp.mac_addr.mutable_data(), info.mac_addr, ETH_MAC_SIZE);

    // mac_role is a bitfield, but only a single value is supported for an interface
    switch (info.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
        resp.role = wlan_mlme::MacRole::CLIENT;
        break;
    case WLAN_MAC_ROLE_AP:
        resp.role = wlan_mlme::MacRole::AP;
        break;
    default:
        // TODO(NET-1116): return an error!
        break;
    }

    resp.bands->resize(0);
    for (uint8_t band_idx = 0; band_idx < info.num_bands; band_idx++) {
        const wlan_band_info_t& band_info = info.bands[band_idx];
        wlan_mlme::BandCapabilities band;
        band.basic_rates->resize(0);
        for (size_t rate_idx = 0; rate_idx < sizeof(band_info.basic_rates); rate_idx++) {
            if (band_info.basic_rates[rate_idx] != 0) {
                band.basic_rates->push_back(band_info.basic_rates[rate_idx]);
            }
        }
        const wlan_chan_list_t& chan_list = band_info.supported_channels;
        band.base_frequency = chan_list.base_freq;
        band.channels->resize(0);
        for (size_t chan_idx = 0; chan_idx < sizeof(chan_list.channels); chan_idx++) {
            if (chan_list.channels[chan_idx] != 0) {
                band.channels->push_back(chan_list.channels[chan_idx]);
            }
        }
        resp.bands->push_back(std::move(band));
    }

    return SendServiceMessage(fuchsia_wlan_mlme_MLMEDeviceQueryConfOrdinal, &resp);
}

zx_status_t Dispatcher::HandleMlmeStats(uint32_t ordinal) const {
    debugfn();
    ZX_DEBUG_ASSERT(ordinal == fuchsia_wlan_mlme_MLMEStatsQueryReqOrdinal);

    wlan_mlme::StatsQueryResponse resp = GetStatsToFidl();
    return SendServiceMessage(fuchsia_wlan_mlme_MLMEStatsQueryRespOrdinal, &resp);
}

void Dispatcher::HwIndication(uint32_t ind) {
    debugfn();
    mlme_->HwIndication(ind);
}

void Dispatcher::ResetStats() {
    stats_.Reset();
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

void Dispatcher::CreateAndStartTelemetry() {
    telemetry_.reset(new Telemetry(this));
    telemetry_->StartWorker(kTelemetryReportPeriodMinutes);
}

}  // namespace wlan
