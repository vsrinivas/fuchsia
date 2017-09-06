// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mlme.h"

#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "scanner.h"
#include "serialize.h"
#include "station.h"
#include "timer.h"
#include "wlan.h"

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <drivers/wifi/common/bitfield.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <mx/time.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

namespace {
enum class ObjectSubtype : uint8_t {
    kTimer = 0,
};

enum class ObjectTarget : uint8_t {
    kScanner = 0,
    kStation = 1,
};

// An ObjectId is used as an id in a PortKey. Therefore, only the lower 56 bits may be used.
class ObjectId : public common::BitField<uint64_t> {
  public:
    constexpr explicit ObjectId(uint64_t id) : common::BitField<uint64_t>(id) {}
    constexpr ObjectId() = default;

    // ObjectSubtype
    WLAN_BIT_FIELD(subtype, 0, 4);
    // ObjectTarget
    WLAN_BIT_FIELD(target, 4, 4);

    // For objects with a MAC address
    WLAN_BIT_FIELD(mac, 8, 48);
};
}  // namespace

Mlme::Mlme(DeviceInterface* device)
  : device_(device) {
    debugfn();
}

Mlme::~Mlme() {}

mx_status_t Mlme::Init() {
    debugfn();

    fbl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kScanner));
    mx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != MX_OK) {
        errorf("could not create scan timer: %d\n", status);
        return status;
    }
    scanner_.reset(new Scanner(device_, std::move(timer)));
    return status;
}

namespace {
void DumpPacket(const Packet& packet) {
    const uint8_t* p = packet.data();
    for (size_t i = 0; i < packet.len(); i++) {
        if (i % 16 == 0) {
            std::printf("\nwlan: ");
        }
        std::printf("%02x ", p[i]);
    }
    std::printf("\n");
}
}  // namespace

mx_status_t Mlme::HandlePacket(const Packet* packet) {
    debugfn();
    MX_DEBUG_ASSERT(packet != nullptr);
    MX_DEBUG_ASSERT(packet->peer() != Packet::Peer::kUnknown);
    debughdr("packet data=%p len=%zu peer=%s\n",
              packet->data(), packet->len(),
              packet->peer() == Packet::Peer::kWlan ? "Wlan" :
              packet->peer() == Packet::Peer::kEthernet ? "Ethernet" :
              packet->peer() == Packet::Peer::kService ? "Service" : "Unknown");

    if (kLogLevel & kLogDataPacketTrace) {
        DumpPacket(*packet);
    }

    mx_status_t status = MX_OK;
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
            status = MX_ERR_NOT_SUPPORTED;
            break;
        }
        break;
    }
    default:
        break;
    }

    return status;
}

mx_status_t Mlme::HandlePortPacket(uint64_t key) {
    debugfn();
    MX_DEBUG_ASSERT(ToPortKeyType(key) == PortKeyType::kMlme);

    ObjectId id(ToPortKeyId(key));
    switch (id.subtype()) {
    case to_enum_type(ObjectSubtype::kTimer):
        switch (id.target()) {
        case to_enum_type(ObjectTarget::kScanner):
            scanner_->HandleTimeout();
            break;
        case to_enum_type(ObjectTarget::kStation):
            MX_DEBUG_ASSERT(sta_ != nullptr);
            if (id.mac() != sta_->bssid()->to_u64()) {
                warnf("timeout for unknown bssid: %" PRIu64 "\n", id.mac());
                break;
            }
            sta_->HandleTimeout();
            break;
        default:
            warnf("unknown MLME timer target: %u\n", id.target());
            break;
        }
        break;
    default:
        warnf("unknown MLME event subtype: %u\n", id.subtype());
    }
    return MX_OK;
}

mx_status_t Mlme::HandleCtrlPacket(const Packet* packet) {
    debugfn();
    return MX_OK;
}

mx_status_t Mlme::HandleDataPacket(const Packet* packet) {
    debugfn();
    if (IsStaValid()) {
        auto hdr = packet->field<DataFrameHeader>(0);
        if (hdr == nullptr) {
            errorf("short data packet len=%zu\n", packet->len());
            return MX_OK;
        }

        if (*sta_->bssid() == hdr->addr2) {
            return sta_->HandleData(packet);
        }
    }
    return MX_OK;
}

mx_status_t Mlme::HandleMgmtPacket(const Packet* packet) {
    debugfn();
    auto hdr = packet->field<MgmtFrameHeader>(0);
    if (hdr == nullptr) {
        errorf("short mgmt packet len=%zu\n", packet->len());
        return MX_OK;
    }
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n",
              hdr->fc.val(), hdr->duration, hdr->sc.seq(), hdr->sc.frag());
    debughdr("dest: " MAC_ADDR_FMT "  source: " MAC_ADDR_FMT "  bssid: " MAC_ADDR_FMT "\n",
              MAC_ADDR_ARGS(hdr->addr1),
              MAC_ADDR_ARGS(hdr->addr2),
              MAC_ADDR_ARGS(hdr->addr3));

    switch (hdr->fc.subtype()) {
    case ManagementSubtype::kBeacon:
        return HandleBeacon(packet);
    case ManagementSubtype::kProbeResponse:
        return HandleProbeResponse(packet);
    case ManagementSubtype::kAuthentication:
        return HandleAuthentication(packet);
    case ManagementSubtype::kDeauthentication:
        return HandleDeauthentication(packet);
    case ManagementSubtype::kAssociationResponse:
        return HandleAssociationResponse(packet);
    case ManagementSubtype::kDisassociation:
        return HandleDisassociation(packet);
    default:
        break;
    }
    return MX_OK;
}

mx_status_t Mlme::HandleEthPacket(const Packet* packet) {
    debugfn();
    return IsStaValid() ? sta_->HandleEth(packet) : MX_OK;
}

mx_status_t Mlme::HandleSvcPacket(const Packet* packet) {
    debugfn();
    const uint8_t* p = packet->data();
    auto h = FromBytes<ServiceHeader>(p, packet->len());
    if (h == nullptr) {
        errorf("short service packet len=%zu\n", packet->len());
        return MX_OK;
    }
    debughdr("service packet txn_id=%" PRIu64 " flags=%u ordinal=%u\n",
              h->txn_id, h->flags, h->ordinal);

    mx_status_t status = MX_OK;
    switch (static_cast<Method>(h->ordinal)) {
    case Method::SCAN_request: {
        ScanRequestPtr req;
        status = DeserializeServiceMsg(*packet, Method::SCAN_request, &req);
        if (status != MX_OK) {
            errorf("could not deserialize ScanRequest: %d\n", status);
            break;
        }

        status = scanner_->Start(std::move(req));
        break;
    }
    case Method::JOIN_request: {
        JoinRequestPtr req;
        status = DeserializeServiceMsg(*packet, Method::JOIN_request, &req);
        if (status != MX_OK) {
            errorf("could not deserialize JoinRequest: %d\n", status);
            break;
        }

        fbl::unique_ptr<Timer> timer;
        ObjectId timer_id;
        timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
        timer_id.set_target(to_enum_type(ObjectTarget::kStation));
        timer_id.set_mac(DeviceAddress(req->selected_bss->bssid.data()).to_u64());
        status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
        if (status != MX_OK) {
            errorf("could not create station timer: %d\n", status);
            return status;
        }
        sta_.reset(new Station(device_, std::move(timer)));
        status = sta_->Join(std::move(req));
        break;
    }
    case Method::AUTHENTICATE_request: {
        // TODO(tkilbourn): send error response back to service is !IsStaValid
        AuthenticateRequestPtr req;
        status = DeserializeServiceMsg(*packet, Method::AUTHENTICATE_request, &req);
        if (status != MX_OK) {
            errorf("could not deserialize AuthenticateRequest: %d\n", status);
            break;
        }

        if (IsStaValid()) {
            status = sta_->Authenticate(std::move(req));
        }
        break;
    }
    case Method::ASSOCIATE_request: {
        // TODO(tkilbourn): send error response back to service is !IsStaValid
        AssociateRequestPtr req;
        status = DeserializeServiceMsg(*packet, Method::ASSOCIATE_request, &req);
        if (status != MX_OK) {
            errorf("could not deserialize AssociateRequest: %d\n", status);
            break;
        }

        if (IsStaValid()) {
            status = sta_->Associate(std::move(req));
        }
        break;
    }
    default:
        warnf("unknown MLME method %u\n", h->ordinal);
        status = MX_ERR_NOT_SUPPORTED;
    }

    return status;
}

mx_status_t Mlme::HandleBeacon(const Packet* packet) {
    debugfn();

    if (scanner_->IsRunning()) {
        scanner_->HandleBeaconOrProbeResponse(packet);
    }

    if (IsStaValid()) {
        auto hdr = packet->field<MgmtFrameHeader>(0);
        if (*sta_->bssid() == hdr->addr3) {
            sta_->HandleBeacon(packet);
        }
    }

    return MX_OK;
}

mx_status_t Mlme::HandleProbeResponse(const Packet* packet) {
    debugfn();

    if (scanner_->IsRunning()) {
        scanner_->HandleBeaconOrProbeResponse(packet);
    }

    return MX_OK;
}

mx_status_t Mlme::HandleAuthentication(const Packet* packet) {
    debugfn();

    if (IsStaValid()) {
        auto hdr = packet->field<MgmtFrameHeader>(0);
        if (*sta_->bssid() == hdr->addr3) {
            sta_->HandleAuthentication(packet);
        }
    }
    return MX_OK;
}

mx_status_t Mlme::HandleDeauthentication(const Packet* packet) {
    debugfn();

    if (IsStaValid()) {
        auto hdr = packet->field<MgmtFrameHeader>(0);
        if (*sta_->bssid() == hdr->addr3) {
            sta_->HandleDeauthentication(packet);
        }
    }
    return MX_OK;
}

mx_status_t Mlme::HandleAssociationResponse(const Packet* packet) {
    debugfn();

    if (IsStaValid()) {
        auto hdr = packet->field<MgmtFrameHeader>(0);
        if (*sta_->bssid() == hdr->addr3) {
            sta_->HandleAssociationResponse(packet);
        }
    }
    return MX_OK;
}

mx_status_t Mlme::HandleDisassociation(const Packet* packet) {
    debugfn();

    if (IsStaValid()) {
        auto hdr = packet->field<MgmtFrameHeader>(0);
        if (*sta_->bssid() == hdr->addr3) {
            sta_->HandleDisassociation(packet);
        }
    }
    return MX_OK;
}

bool Mlme::IsStaValid() const {
    return sta_ != nullptr && sta_->bssid() != nullptr;
}

mx_status_t Mlme::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    if (IsStaValid()) {
        sta_->PreChannelChange(chan);
    }
    return MX_OK;
}

mx_status_t Mlme::PostChannelChange() {
    debugfn();
    if (IsStaValid()) {
        sta_->PostChannelChange();
    }
    return MX_OK;
}

}  // namespace wlan
