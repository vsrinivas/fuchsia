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
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <wlan_mlme/c/fidl.h>
#include <wlan_mlme/cpp/fidl.h>
#include <wlan_stats/c/fidl.h>

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

Dispatcher::Dispatcher(DeviceInterface* device, fbl::unique_ptr<Mlme> mlme)
    : device_(device), mlme_(std::move(mlme)) {
    debugfn();
    ZX_ASSERT(mlme_ != nullptr);
}

Dispatcher::~Dispatcher() {}

template <>
zx_status_t Dispatcher::HandleMlmeMethod<wlan_mlme::DeviceQueryRequest>(
    fbl::unique_ptr<Packet> packet, uint32_t ordinal);

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
    if (mlme_ == nullptr && !service_msg) { return ZX_OK; }

    zx_status_t status = ZX_OK;
    switch (packet->peer()) {
    case Packet::Peer::kService:
        status = HandleSvcPacket(fbl::move(packet));
        break;
    case Packet::Peer::kEthernet:
        status = HandleEthPacket(fbl::move(packet));
        break;
    case Packet::Peer::kWlan: {
        auto fc = packet->field<FrameControl>(0);

        // TODO(porce): Handle HTC field.
        if (fc->HasHtCtrl()) {
            warnf("WLAN frame (type %u:%u) HTC field is present but not handled. Drop.", fc->type(),
                  fc->subtype());
            status = ZX_ERR_NOT_SUPPORTED;
            break;
        }

        switch (fc->type()) {
        case FrameType::kManagement:
            WLAN_STATS_INC(mgmt_frame.in);
            status = HandleMgmtPacket(fbl::move(packet));
            break;
        case FrameType::kControl:
            WLAN_STATS_INC(ctrl_frame.in);
            status = HandleCtrlPacket(fbl::move(packet));
            break;
        case FrameType::kData:
            WLAN_STATS_INC(data_frame.in);
            status = HandleDataPacket(fbl::move(packet));
            break;
        default:
            warnf("unknown MAC frame type %u\n", fc->type());
            status = ZX_ERR_NOT_SUPPORTED;
            break;
        }
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

zx_status_t Dispatcher::HandleCtrlPacket(fbl::unique_ptr<Packet> packet) {
    debugfn();

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    ImmutableFrame<FrameControl, UnknownBody> ctrl_frame(fbl::move(packet));
    if (!ctrl_frame.HasValidLen()) {
        errorf("short control frame len=%zu\n", ctrl_frame.take()->len());
        return ZX_OK;
    }

    auto fc = ctrl_frame.hdr();
    switch (fc->subtype()) {
    case ControlSubtype::kPsPoll: {
        ImmutableCtrlFrame<PsPollFrame> ps_poll(ctrl_frame.take());
        if (!ps_poll.HasValidLen()) {
            errorf("short ps poll frame len=%zu\n", ps_poll.take()->len());
            return ZX_OK;
        }
        return mlme_->HandleFrame(ps_poll, *rxinfo);
    }
    default:
        debugf("rxed unfiltered control subtype 0x%02x\n", fc->subtype());
        return ZX_OK;
    }
}

zx_status_t Dispatcher::HandleDataPacket(fbl::unique_ptr<Packet> packet) {
    debugfn();

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    ImmutableDataFrame<UnknownBody> data_frame(fbl::move(packet));
    if (!data_frame.HasValidLen()) {
        errorf("short data packet len=%zu\n", data_frame.take()->len());
        return ZX_OK;
    }

    auto hdr = data_frame.hdr();
    switch (hdr->fc.subtype()) {
    case DataSubtype::kNull:
        // Fall-through
    case DataSubtype::kQosnull: {
        ImmutableDataFrame<NilHeader> null_frame(data_frame.take());
        return mlme_->HandleFrame(null_frame, *rxinfo);
    }
    case DataSubtype::kDataSubtype:
        // Fall-through
    case DataSubtype::kQosdata:
        break;
    default:
        warnf("unsupported data subtype %02x\n", hdr->fc.subtype());
        return ZX_OK;
    }

    ImmutableDataFrame<LlcHeader> llc(data_frame.take());
    if (!llc.HasValidLen()) {
        errorf("short data packet len=%zu\n", llc.take()->len());
        return ZX_ERR_IO;
    }
    return mlme_->HandleFrame(llc, *rxinfo);
}

zx_status_t Dispatcher::HandleMgmtPacket(fbl::unique_ptr<Packet> packet) {
    debugfn();

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    ImmutableMgmtFrame<UnknownBody> mgmt_frame(fbl::move(packet));
    if (!mgmt_frame.HasValidLen()) {
        errorf("short mgmt packet len=%zu\n", mgmt_frame.take()->len());
        return ZX_OK;
    }

    auto hdr = mgmt_frame.hdr();
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n", hdr->fc.val(), hdr->duration,
             hdr->sc.seq(), hdr->sc.frag());

    const common::MacAddr& dst = hdr->addr1;
    const common::MacAddr& src = hdr->addr2;
    const common::MacAddr& bssid = hdr->addr3;

    debughdr("dest: %s source: %s bssid: %s\n", MACSTR(dst), MACSTR(src), MACSTR(bssid));

    switch (hdr->fc.subtype()) {
    case ManagementSubtype::kBeacon: {
        ImmutableMgmtFrame<Beacon> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("beacon packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kProbeResponse: {
        ImmutableMgmtFrame<ProbeResponse> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("probe response packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kProbeRequest: {
        ImmutableMgmtFrame<ProbeRequest> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("probe request packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAuthentication: {
        ImmutableMgmtFrame<Authentication> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("authentication packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kDeauthentication: {
        ImmutableMgmtFrame<Deauthentication> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("deauthentication packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAssociationRequest: {
        ImmutableMgmtFrame<AssociationRequest> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("assocation request packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAssociationResponse: {
        ImmutableMgmtFrame<AssociationResponse> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("assocation response packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kDisassociation: {
        ImmutableMgmtFrame<Disassociation> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("disassociation packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAction: {
        ImmutableMgmtFrame<ActionFrame> frame(mgmt_frame.take());
        if (!frame.HasValidLen()) {
            errorf("action packet too small (len=%zd)\n", frame.take()->len());
            return ZX_ERR_IO;
        }
        if (!frame.hdr()->IsAction()) {
            errorf("action packet is not an action\n");
            return ZX_ERR_IO;
        }
        HandleActionPacket(fbl::move(frame), rxinfo);
    }
    default:
        if (!dst.IsBcast()) {
            // TODO(porce): Evolve this logic to support AP role.
            debugf("Rxed Mgmt frame (type: %d) but not handled\n", hdr->fc.subtype());
        }
        break;
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleActionPacket(ImmutableMgmtFrame<ActionFrame> action_frame,
                                           const wlan_rx_info_t* rxinfo) {
    if (action_frame.body()->category != action::Category::kBlockAck) {
        verbosef("Rxed Action frame with category %d. Not handled.\n",
                 action_frame.body()->category);
        return ZX_OK;
    }

    ImmutableMgmtFrame<ActionFrameBlockAck> ba_frame(action_frame.take());
    if (!ba_frame.HasValidLen()) {
        errorf("bloackack packet too small (len=%zd)\n", ba_frame.take()->len());
        return ZX_ERR_IO;
    }

    switch (ba_frame.body()->action) {
    case action::BaAction::kAddBaRequest: {
        ImmutableMgmtFrame<AddBaRequestFrame> addbar(ba_frame.take());
        if (!addbar.HasValidLen()) {
            errorf("addbar packet too small (len=%zd)\n", addbar.take()->len());
            return ZX_ERR_IO;
        }

        // TODO(porce): Support AddBar. Work with lower mac.
        // TODO(porce): Make this conditional depending on the hardware capability.

        return mlme_->HandleFrame(addbar, *rxinfo);
    }
    case action::BaAction::kAddBaResponse: {
        ImmutableMgmtFrame<AddBaResponseFrame> addba_resp(ba_frame.take());
        if (!addba_resp.HasValidLen()) {
            errorf("addba_resp packet too small (len=%zd)\n", addba_resp.take()->len());
            return ZX_ERR_IO;
        }
        return mlme_->HandleFrame(addba_resp, *rxinfo);
    }
    case action::BaAction::kDelBa:
    // fall-through
    default:
        warnf("BlockAck action frame with action %u not handled.\n", ba_frame.body()->action);
        break;
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleEthPacket(fbl::unique_ptr<Packet> packet) {
    debugfn();

    ImmutableBaseFrame<EthernetII> eth_frame(fbl::move(packet));
    if (!eth_frame.HasValidLen()) {
        errorf("short ethernet frame len=%zu\n", eth_frame.take()->len());
        return ZX_ERR_IO;
    }
    return mlme_->HandleFrame(eth_frame);
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

    if (hdr->ordinal == wlan_mlme_MLMEDeviceQueryReqOrdinal) {
        return HandleMlmeMethod<wlan_mlme::DeviceQueryRequest>(fbl::move(packet), hdr->ordinal);
    }

    switch (hdr->ordinal) {
    case wlan_mlme_MLMEResetReqOrdinal:
        infof("resetting MLME\n");
        HandleMlmeMethod<wlan_mlme::ResetRequest>(fbl::move(packet), hdr->ordinal);
        return ZX_OK;
    case wlan_mlme_MLMEStartReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::StartRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEStopReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::StopRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEScanReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::ScanRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEJoinReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::JoinRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEAuthenticateReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::AuthenticateRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEAuthenticateRespOrdinal:
        return HandleMlmeMethod<wlan_mlme::AuthenticateResponse>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEDeauthenticateReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::DeauthenticateRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEAssociateReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::AssociateRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEAssociateRespOrdinal:
        return HandleMlmeMethod<wlan_mlme::AssociateResponse>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMEEapolReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::EapolRequest>(fbl::move(packet), hdr->ordinal);
    case wlan_mlme_MLMESetKeysReqOrdinal:
        return HandleMlmeMethod<wlan_mlme::SetKeysRequest>(fbl::move(packet), hdr->ordinal);
    default:
        warnf("unknown MLME method %u\n", hdr->ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

template <typename Message>
zx_status_t Dispatcher::HandleMlmeMethod(fbl::unique_ptr<Packet> packet, uint32_t ordinal) {
    Message msg;
    auto status = DeserializeServiceMsg<Message>(*packet, ordinal, &msg);
    if (status != ZX_OK) {
        errorf("could not deserialize MLME Method %d: %d\n", ordinal, status);
        return status;
    }
    return mlme_->HandleFrame(ordinal, msg);
}

template <>
zx_status_t Dispatcher::HandleMlmeMethod<wlan_mlme::DeviceQueryRequest>(fbl::unique_ptr<Packet> _,
                                                                        uint32_t ordinal) {
    debugfn();
    ZX_DEBUG_ASSERT(ordinal == wlan_mlme_MLMEDeviceQueryReqOrdinal);

    wlan_mlme::DeviceQueryConfirm resp;
    const wlanmac_info_t& info = device_->GetWlanInfo();

    memcpy(resp.mac_addr.mutable_data(), info.eth_info.mac, ETH_MAC_SIZE);

    switch (info.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
        resp.role = wlan_mlme::MacRole::CLIENT;
        break;
    case WLAN_MAC_ROLE_AP:
        resp.role = wlan_mlme::MacRole::AP;
        break;
    default:
        // TODO(tkilbourn): return an error?
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

    // fidl2 doesn't have a way to get the serialized size yet. 4096 bytes should be enough for
    // everyone.
    size_t buf_len = 4096;
    // size_t buf_len = sizeof(fidl_message_header_t) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status =
        SerializeServiceMsg(packet.get(), wlan_mlme_MLMEDeviceQueryConfOrdinal, &resp);
    if (status != ZX_OK) {
        errorf("could not serialize DeviceQueryResponse: %d\n", status);
        return status;
    }

    return device_->SendService(std::move(packet));
}

zx_status_t Dispatcher::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    mlme_->PreChannelChange(chan);
    return ZX_OK;
}

zx_status_t Dispatcher::PostChannelChange() {
    debugfn();
    mlme_->PostChannelChange();
    return ZX_OK;
}

void Dispatcher::HwIndication(uint32_t ind) {
    debugfn();
    mlme_->HwIndication(ind);
}

}  // namespace wlan
