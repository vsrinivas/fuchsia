// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ap_mlme.h"
#include "dispatcher.h"
#include "client_mlme.h"
#include "frame_handler.h"
#include "packet.h"
#include "serialize.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"
#include "lib/wlan/fidl/wlan_mlme_ext.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

namespace {

template <unsigned int N, typename T> T align(T t) {
    static_assert(N > 1 && !(N & (N - 1)), "alignment must be with a power of 2");
    return (t + (N - 1)) & ~(N - 1);
}

void DumpPacket(const Packet& packet) {
    const uint8_t* p = packet.data();
    for (size_t i = 0; i < packet.len(); i++) {
        if (i % 16 == 0) { std::printf("\nwlan: "); }
        std::printf("%02x ", p[i]);
    }
    std::printf("\n");
}

void DumpRxInfo(const wlan_rx_info_t& rxinfo) {
    std::printf(
        "WLAN RxInfo: "
        "flags %08x valid_fields %08x phy %u chan_width %u data_rate %u chan %u "
        "mcs %u rssi %u rcpi %u snr %u \n",
        rxinfo.rx_flags, rxinfo.valid_fields, rxinfo.phy, rxinfo.chan_width, rxinfo.data_rate,
        rxinfo.chan.channel_num, rxinfo.mcs, rxinfo.rssi, rxinfo.rcpi, rxinfo.snr);
}

void DumpFrameHeader(const FrameHeader& hdr, size_t len) {
    // TODO(porce): Introspect the frame type in general, and support Control Frames.
    std::printf(
        "WLAN Frame:  Len %zu"
        "\n       "
        "Proto %u Type %u Subtype %u ToDs %u FromDs %u Frag %u Retry %u PwrMgmt %u MoreData %u "
        "Protected %u Htc %u Duration %u Seq [%u:%u]"
        "\n       "
        "[Addr1] %s  [Addr2] %s  [Addr3] %s"
        "\n",
        len, hdr.fc.protocol_version(), hdr.fc.type(), hdr.fc.subtype(), hdr.fc.to_ds(),
        hdr.fc.from_ds(), hdr.fc.more_frag(), hdr.fc.retry(), hdr.fc.pwr_mgmt(), hdr.fc.more_data(),
        hdr.fc.protected_frame(), hdr.fc.htc_order(), hdr.duration, hdr.sc.frag(), hdr.sc.seq(),
        MACSTR(hdr.addr1), MACSTR(hdr.addr2), MACSTR(hdr.addr3));
}

#define DEBUG_DUMP_WLAN_FRAME(packet)                          \
    do {                                                       \
        if (kLogLevel & kLogWlanFrameTrace) {                  \
            auto rxinfo = packet->ctrl_data<wlan_rx_info_t>(); \
            DumpRxInfo(*rxinfo);                               \
            auto hdr = packet->field<FrameHeader>(0);          \
            DumpFrameHeader(*hdr, packet->len());              \
        }                                                      \
    } while (false)

}  // namespace

Dispatcher::Dispatcher(DeviceInterface* device) : device_(device) {
    debugfn();
}

Dispatcher::~Dispatcher() {}

template <>
zx_status_t Dispatcher::HandleMlmeMethod<DeviceQueryRequest>(const Packet* packet, Method method);

zx_status_t Dispatcher::HandlePacket(const Packet* packet) {
    debugfn();

    ZX_DEBUG_ASSERT(packet != nullptr);
    ZX_DEBUG_ASSERT(packet->peer() != Packet::Peer::kUnknown);
    debughdr("packet data=%p len=%zu peer=%s\n", packet->data(), packet->len(),
             packet->peer() == Packet::Peer::kWlan
                 ? "Wlan"
                 : packet->peer() == Packet::Peer::kEthernet
                       ? "Ethernet"
                       : packet->peer() == Packet::Peer::kService ? "Service" : "Unknown");

    if (kLogLevel & kLogDataPacketTrace) { DumpPacket(*packet); }

    // If there is no active MLME, block all packets but service ones.
    // MLME-JOIN.request and MLME-START.request implicitly select a mode and initialize the MLME.
    // DEVICE_QUERY.request is used to obtain device capabilities.
    auto service_msg = (packet->peer() == Packet::Peer::kService);
    if (mlme_ == nullptr && !service_msg) {
        errorf("received packet with no active MLME\n");
        return ZX_OK;
    }

    zx_status_t status = ZX_OK;
    switch (packet->peer()) {
    case Packet::Peer::kService:
        status = HandleSvcPacket(packet);
        break;
    case Packet::Peer::kEthernet:
        status = HandleEthPacket(packet);
        break;
    case Packet::Peer::kWlan: {
        auto fc = packet->field<FrameControl>(0);
        debughdr("FrameControl type: %u subtype: %u\n", fc->type(), fc->subtype());

        // TODO(porce): Handle HTC field.
        if (fc->HasHtCtrl()) {
            warnf("WLAN frame (type %u:%u) HTC field is present but not handled. Drop.", fc->type(),
                  fc->subtype());
            status = ZX_ERR_NOT_SUPPORTED;
            break;
        }

        switch (fc->type()) {
        case FrameType::kManagement:
            DEBUG_DUMP_WLAN_FRAME(packet);
            status = HandleMgmtPacket(packet);
            break;
        case FrameType::kControl:
            status = HandleCtrlPacket(packet);
            break;
        case FrameType::kData:
            DEBUG_DUMP_WLAN_FRAME(packet);
            status = HandleDataPacket(packet);
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

zx_status_t Dispatcher::HandleCtrlPacket(const Packet* packet) {
    debugfn();

    // Currently not used.
    return ZX_OK;
}

zx_status_t Dispatcher::HandleDataPacket(const Packet* packet) {
    debugfn();

    auto hdr = packet->field<DataFrameHeader>(0);
    if (hdr == nullptr) {
        errorf("short data packet len=%zu\n", packet->len());
        return ZX_OK;
    }

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    switch (hdr->fc.subtype()) {
    case DataSubtype::kNull:
        // TODO(hahnr): Use DataFrame with an empty body rather than the header directly.
        return mlme_->HandleFrame(*hdr, *rxinfo);
    case DataSubtype::kDataSubtype:
        // Fall-through
    case DataSubtype::kQosdata:
        break;
    default:
        warnf("unsupported data subtype %02x\n", hdr->fc.subtype());
        return ZX_OK;
    }

    auto llc_offset = hdr->len();
    if (rxinfo->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4) {
        llc_offset = align<4>(llc_offset);
    }

    auto llc = packet->field<LlcHeader>(llc_offset);
    if (llc == nullptr) {
        errorf("short data packet len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    if (packet->len() < kDataPayloadHeader) {
        errorf("short LLC packet len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    size_t llc_len = packet->len() - llc_offset;
    auto frame = DataFrame<LlcHeader>(hdr, llc, llc_len);
    return mlme_->HandleFrame(frame, *rxinfo);
}

zx_status_t Dispatcher::HandleMgmtPacket(const Packet* packet) {
    debugfn();

    auto hdr = packet->field<MgmtFrameHeader>(0);
    if (hdr == nullptr) {
        errorf("short mgmt packet len=%zu\n", packet->len());
        return ZX_OK;
    }
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n", hdr->fc.val(), hdr->duration,
             hdr->sc.seq(), hdr->sc.frag());

    const common::MacAddr& dst = hdr->addr1;
    const common::MacAddr& src = hdr->addr2;
    const common::MacAddr& bssid = hdr->addr3;

    debughdr("dest: %s source: %s bssid: %s\n", MACSTR(dst), MACSTR(src), MACSTR(bssid));

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    size_t payload_len = packet->len() - hdr->len();

    switch (hdr->fc.subtype()) {
    case ManagementSubtype::kBeacon: {
        auto beacon = packet->field<Beacon>(hdr->len());
        if (beacon == nullptr) {
            errorf("beacon packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<Beacon>(hdr, beacon, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kProbeResponse: {
        auto proberesp = packet->field<ProbeResponse>(hdr->len());
        if (proberesp == nullptr) {
            errorf("probe response packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<ProbeResponse>(hdr, proberesp, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAuthentication: {
        auto auth = packet->field<Authentication>(hdr->len());
        if (auth == nullptr) {
            errorf("authentication packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<Authentication>(hdr, auth, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kDeauthentication: {
        auto deauth = packet->field<Deauthentication>(hdr->len());
        if (deauth == nullptr) {
            errorf("deauthentication packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<Deauthentication>(hdr, deauth, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAssociationResponse: {
        auto authresp = packet->field<AssociationResponse>(hdr->len());
        if (authresp == nullptr) {
            errorf("assocation response packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<AssociationResponse>(hdr, authresp, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kDisassociation: {
        auto disassoc = packet->field<Disassociation>(hdr->len());
        if (disassoc == nullptr) {
            errorf("disassociation packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<Disassociation>(hdr, disassoc, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
    }
    case ManagementSubtype::kAction: {
        auto action = packet->field<ActionFrame>(hdr->len());
        if (action == nullptr) {
            errorf("action packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        if (!hdr->IsAction()) {
            errorf("action packet is not an action\n");
            return ZX_ERR_IO;
        }
        HandleActionPacket(packet, hdr, action, rxinfo);
    }
    default:
        if (!dst.IsBcast()) {
            // TODO(porce): Evolve this logic to support AP mode.
            debugf("Rxed Mgmt frame (type: %d) but not handled\n", hdr->fc.subtype());
        }
        break;
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleActionPacket(const Packet* packet, const MgmtFrameHeader* hdr,
                                           const ActionFrame* action,
                                           const wlan_rx_info_t* rxinfo) {
    if (action->category != action::Category::kBlockAck) {
        verbosef("Rxed Action frame with category %d. Not handled.\n", action->category);
        return ZX_OK;
    }

    size_t payload_len = packet->len() - hdr->len();
    auto ba_frame = packet->field<ActionFrameBlockAck>(hdr->len());
    if (ba_frame == nullptr) {
        errorf("bloackack packet too small (len=%zd)\n", payload_len);
        return ZX_ERR_IO;
    }

    switch (ba_frame->action) {
    case action::BaAction::kAddBaRequest: {
        auto addbar = packet->field<AddBaRequestFrame>(hdr->len());
        if (addbar == nullptr) {
            errorf("addbar packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }

        // TODO(porce): Support AddBar. Work with lower mac.
        // TODO(porce): Make this conditional depending on the hardware capability.

        auto frame = MgmtFrame<AddBaRequestFrame>(hdr, addbar, payload_len);
        return mlme_->HandleFrame(frame, *rxinfo);
        break;
    }
    case action::BaAction::kAddBaResponse:
    // fall-through
    case action::BaAction::kDelBa:
    // fall-through
    default:
        warnf("BlockAck action frame with action %u not handled.\n", ba_frame->action);
        break;
    }
    return ZX_OK;
}

zx_status_t Dispatcher::HandleEthPacket(const Packet* packet) {
    debugfn();

    auto hdr = packet->field<EthernetII>(0);
    if (hdr == nullptr) {
        errorf("short ethernet frame len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }

    auto payload = packet->field<uint8_t>(sizeof(hdr));
    size_t payload_len = packet->len() - sizeof(hdr);
    auto frame = BaseFrame<EthernetII>(hdr, payload, payload_len);
    return mlme_->HandleFrame(frame);
}

zx_status_t Dispatcher::HandleSvcPacket(const Packet* packet) {
    debugfn();

    const uint8_t* bytes = packet->data();
    auto hdr = FromBytes<ServiceHeader>(bytes, packet->len());
    if (hdr == nullptr) {
        errorf("short service packet len=%zu\n", packet->len());
        return ZX_OK;
    }
    debughdr("service packet txn_id=%" PRIu64 " flags=%u ordinal=%u\n", hdr->txn_id, hdr->flags,
             hdr->ordinal);

    auto method = static_cast<Method>(hdr->ordinal);

    if (method == Method::DEVICE_QUERY_request) {
        return HandleMlmeMethod<DeviceQueryRequest>(packet, method);
    }

    // Only a subset of requests are supported before an MLME has been initialized.
    if (mlme_ == nullptr) {
        switch (method) {
            case Method::SCAN_request:
            // fallthrough
            case Method::JOIN_request: {
                mlme_.reset(new ClientMlme(device_));
                auto status = mlme_->Init();
                if (status != ZX_OK) {
                    errorf("Client MLME could not be initialized\n");
                    mlme_.reset();
                    return status;
                }
                break;
            }
            case Method::START_request: {
                mlme_.reset(new ApMlme(device_));
                auto status = mlme_->Init();
                if (status != ZX_OK) {
                    errorf("AP MLME could not be initialized\n");
                    mlme_.reset();
                    return status;
                }
                break;
            }
            default:
                warnf("unknown MLME method %u with no active MLME\n", method);
                return ZX_OK;
        }
    }

    switch (method) {
    case Method::RESET_request:
        // Let currently active MLME handle RESET request, then, reset MLME.
        HandleMlmeMethod<ResetRequest>(packet, method);
        mlme_.reset();
        return ZX_OK;
    case Method::SCAN_request:
        return HandleMlmeMethod<ScanRequest>(packet, method);
    case Method::JOIN_request:
        return HandleMlmeMethod<JoinRequest>(packet, method);
    case Method::AUTHENTICATE_request:
        return HandleMlmeMethod<AuthenticateRequest>(packet, method);
    case Method::DEAUTHENTICATE_request:
        return HandleMlmeMethod<DeauthenticateRequest>(packet, method);
    case Method::ASSOCIATE_request:
        return HandleMlmeMethod<AssociateRequest>(packet, method);
    case Method::EAPOL_request:
        return HandleMlmeMethod<EapolRequest>(packet, method);
    case Method::SETKEYS_request:
        return HandleMlmeMethod<SetKeysRequest>(packet, method);
    default:
        warnf("unknown MLME method %u\n", hdr->ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

template <typename Message>
zx_status_t Dispatcher::HandleMlmeMethod(const Packet* packet, Method method) {
    ::fidl::StructPtr<Message> req;
    auto status = DeserializeServiceMsg(*packet, method, &req);
    if (status != ZX_OK) {
        errorf("could not deserialize MLME Method %d: %d\n", method, status);
        return status;
    }
    ZX_DEBUG_ASSERT(!req.is_null());
    return mlme_->HandleFrame(method, *req);
}

template <>
zx_status_t Dispatcher::HandleMlmeMethod<DeviceQueryRequest>(const Packet* unused_packet, Method method) {
    debugfn();
    ZX_DEBUG_ASSERT(method == Method::DEVICE_QUERY_request);

    auto resp = DeviceQueryResponse::New();
    const wlanmac_info_t& info = device_->GetWlanInfo();
    if (info.mac_modes & WLAN_MAC_MODE_STA) {
        resp->modes.push_back(MacMode::STA);
    }
    if (info.mac_modes & WLAN_MAC_MODE_AP) {
        resp->modes.push_back(MacMode::AP);
    }
    for (uint8_t band_idx = 0; band_idx < info.num_bands; band_idx++) {
        const wlan_band_info_t& band_info = info.bands[band_idx];
        auto band = BandCapabilities::New();
        band->basic_rates.resize(0);
        for (size_t rate_idx = 0; rate_idx < sizeof(band_info.basic_rates); rate_idx++) {
            if (band_info.basic_rates[rate_idx] != 0) {
                band->basic_rates.push_back(band_info.basic_rates[rate_idx]);
            }
        }
        const wlan_chan_list_t& chan_list = band_info.supported_channels;
        band->base_frequency = chan_list.base_freq;
        band->channels.resize(0);
        for (size_t chan_idx = 0; chan_idx < sizeof(chan_list.channels); chan_idx++) {
            if (chan_list.channels[chan_idx] != 0) {
                band->channels.push_back(chan_list.channels[chan_idx]);
            }
        }
        resp->bands.push_back(std::move(band));
    }

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::DEVICE_QUERY_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize DeviceQueryResponse: %d\n", status);
        return status;
    }

    return device_->SendService(std::move(packet));
}

zx_status_t Dispatcher::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    if (mlme_ != nullptr) {
        mlme_->PreChannelChange(chan);
    }
    return ZX_OK;
}

zx_status_t Dispatcher::PostChannelChange() {
    debugfn();
    if (mlme_ != nullptr) {
        mlme_->PostChannelChange();
    }
    return ZX_OK;
}

}  // namespace wlan
