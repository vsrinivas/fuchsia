// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatcher.h"
#include "client_mlme.h"
#include "packet.h"
#include "serialize.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

namespace {
void DumpPacket(const Packet& packet) {
    const uint8_t* p = packet.data();
    for (size_t i = 0; i < packet.len(); i++) {
        if (i % 16 == 0) { std::printf("\nwlan: "); }
        std::printf("%02x ", p[i]);
    }
    std::printf("\n");
}
}  // namespace

Dispatcher::Dispatcher(DeviceInterface* device) {
    debugfn();
    mlme_.reset(new ClientMlme(device));
}

Dispatcher::~Dispatcher() {}

zx_status_t Dispatcher::Init() {
    debugfn();

    return mlme_->Init();
}

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
            status = HandleMgmtPacket(packet);
            break;
        case FrameType::kControl:
            status = HandleCtrlPacket(packet);
            break;
        case FrameType::kData:
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

    if (hdr->fc.subtype() == kNull) { return mlme_->HandleNullDataFrame(hdr, rxinfo); }

    if (hdr->fc.subtype() != 0) {
        warnf("unsupported data subtype %02x\n", hdr->fc.subtype());
        return ZX_OK;
    }

    auto llc = packet->field<LlcHeader>(hdr->len());
    if (llc == nullptr) {
        errorf("short data packet len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    ZX_DEBUG_ASSERT(packet->len() >= kDataPayloadHeader);

    size_t llc_len = packet->len() - hdr->len();
    auto frame = DataFrame<LlcHeader>{.hdr = hdr, .body = llc, .body_len = llc_len};
    return mlme_->HandleDataFrame(&frame, rxinfo);
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
        auto frame = MgmtFrame<Beacon>{.hdr = hdr, .body = beacon, .body_len = payload_len};
        return mlme_->HandleBeacon(&frame, rxinfo);
    }
    case ManagementSubtype::kProbeResponse: {
        auto proberesp = packet->field<ProbeResponse>(hdr->len());
        if (proberesp == nullptr) {
            errorf("probe response packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame =
            MgmtFrame<ProbeResponse>{.hdr = hdr, .body = proberesp, .body_len = payload_len};
        return mlme_->HandleProbeResponse(&frame, rxinfo);
    }
    case ManagementSubtype::kAuthentication: {
        auto auth = packet->field<Authentication>(hdr->len());
        if (auth == nullptr) {
            errorf("authentication packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame = MgmtFrame<Authentication>{.hdr = hdr, .body = auth, .body_len = payload_len};
        return mlme_->HandleAuthentication(&frame, rxinfo);
    }
    case ManagementSubtype::kDeauthentication: {
        auto deauth = packet->field<Deauthentication>(hdr->len());
        if (deauth == nullptr) {
            errorf("deauthentication packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame =
            MgmtFrame<Deauthentication>{.hdr = hdr, .body = deauth, .body_len = payload_len};
        return mlme_->HandleDeauthentication(&frame, rxinfo);
    }
    case ManagementSubtype::kAssociationResponse: {
        auto authresp = packet->field<AssociationResponse>(hdr->len());
        if (authresp == nullptr) {
            errorf("assocation response packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame =
            MgmtFrame<AssociationResponse>{.hdr = hdr, .body = authresp, .body_len = payload_len};
        return mlme_->HandleAssociationResponse(&frame, rxinfo);
    }
    case ManagementSubtype::kDisassociation: {
        auto disassoc = packet->field<Disassociation>(hdr->len());
        if (disassoc == nullptr) {
            errorf("disassociation packet too small (len=%zd)\n", payload_len);
            return ZX_ERR_IO;
        }
        auto frame =
            MgmtFrame<Disassociation>{.hdr = hdr, .body = disassoc, .body_len = payload_len};
        return mlme_->HandleDisassociation(&frame, rxinfo);
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

        auto frame =
            MgmtFrame<AddBaRequestFrame>{.hdr = hdr, .body = addbar, .body_len = payload_len};
        return mlme_->HandleAddBaRequest(&frame, rxinfo);
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
    auto frame = BaseFrame<EthernetII>{.hdr = hdr, .body = payload, .body_len = payload_len};
    return mlme_->HandleEthFrame(&frame);
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

    // TODO(hahnr): Add logic to switch between Client and AP mode.

    auto method = static_cast<Method>(hdr->ordinal);
    switch (method) {
    case Method::SCAN_request: {
        ScanRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::SCAN_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize ScanRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeScanReq(std::move(req));
    }
    case Method::JOIN_request: {
        JoinRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::JOIN_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize JoinRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeJoinReq(std::move(req));
    }
    case Method::AUTHENTICATE_request: {
        AuthenticateRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::AUTHENTICATE_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize AuthenticateRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeAuthReq(std::move(req));
    }
    case Method::DEAUTHENTICATE_request: {
        DeauthenticateRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::DEAUTHENTICATE_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize DeauthenticateRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeDeauthReq(std::move(req));
    }
    case Method::ASSOCIATE_request: {
        AssociateRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::ASSOCIATE_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize AssociateRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeAssocReq(std::move(req));
    }
    case Method::EAPOL_request: {
        EapolRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::EAPOL_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize EapolRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeEapolReq(std::move(req));
    }
    case Method::SETKEYS_request: {
        SetKeysRequestPtr req;
        auto status = DeserializeServiceMsg(*packet, Method::SETKEYS_request, &req);
        if (status != ZX_OK) {
            errorf("could not deserialize SetKeysRequest: %d\n", status);
            return status;
        }
        ZX_DEBUG_ASSERT(!req.is_null());
        return mlme_->HandleMlmeSetKeysReq(std::move(req));
    }
    default:
        warnf("unknown MLME method %u\n", hdr->ordinal);
        return ZX_ERR_NOT_SUPPORTED;
    }
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

}  // namespace wlan
