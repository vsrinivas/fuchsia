// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mlme.h"

#include "device.h"
#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
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

#define MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDR_ARGS(a) ((a)[0]), ((a)[1]), ((a)[2]), ((a)[3]), ((a)[4]), ((a)[5])

namespace wlan {

namespace {
#define BIT_FIELD(name, offset, len) \
    void set_##name(uint32_t val) { this->template set_bits<offset, len>(val); } \
    uint32_t name() const { return this->template get_bits<offset, len>(); }

enum class ObjectSubtype : uint8_t {
    kTimer = 0,
};

enum class ObjectTarget : uint8_t {
    kScanner = 0,
};

// An ObjectId is used as an id in a PortKey. Therefore, only the lower 56 bits may be used.
class ObjectId : public common::BitField<uint64_t> {
  public:
    constexpr explicit ObjectId(uint64_t id) : common::BitField<uint64_t>(id) {}
    constexpr ObjectId() = default;

    // ObjectSubtype
    BIT_FIELD(subtype, 0, 4);
    // ObjectTarget
    BIT_FIELD(target, 4, 4);

    // For objects with a MAC address
    BIT_FIELD(mac, 8, 48);
};

#undef BIT_FIELD
}  // namespace

Mlme::Mlme(DeviceInterface* device, ddk::WlanmacProtocolProxy wlanmac_proxy)
  : device_(device),
    wlanmac_proxy_(wlanmac_proxy) {
    debugfn();
}

mx_status_t Mlme::Init() {
    debugfn();
    mxtl::unique_ptr<Timer> timer;
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kScanner));
    mx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
    if (status != MX_OK) {
        errorf("could not create scan timer: %d\n", status);
        return status;
    }
    scanner_.reset(new Scanner(std::move(timer)));
    return wlanmac_proxy_.Query(0, &ethmac_info_);
}

mx_status_t Mlme::Start(mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac, Device* device) {
    debugfn();
    if (ethmac_proxy_ != nullptr) {
        return MX_ERR_ALREADY_BOUND;
    }
    mx_status_t status = wlanmac_proxy_.Start(device);
    if (status != MX_OK) {
        errorf("could not start wlanmac: %d\n", status);
    } else {
        ethmac_proxy_.swap(ethmac);
    }
    return status;
}

void Mlme::Stop() {
    debugfn();
    service_ = MX_HANDLE_INVALID;
    if (ethmac_proxy_ == nullptr) {
        warnf("ethmac not started\n");
    }
    ethmac_proxy_.reset();
}

void Mlme::SetServiceChannel(mx_handle_t h) {
    debugfn();
    service_ = h;
}

void Mlme::GetDeviceInfo(ethmac_info_t* info) {
    debugfn();
    *info = ethmac_info_;
    // Make sure this device is reported as a wlan device
    info->features |= ETHMAC_FEATURE_WLAN;
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
    debugf("packet data=%p len=%zu peer=%s\n",
            packet->data(), packet->len(),
            packet->peer() == Packet::Peer::kWlan ? "Wlan" :
            packet->peer() == Packet::Peer::kEthernet ? "Ethernet" :
            packet->peer() == Packet::Peer::kService ? "Service" : "Unknown");

    if (kLogLevel >= kLogDebug) {
        DumpPacket(*packet);
    }

    mx_status_t status = MX_OK;
    switch (packet->peer()) {
    case Packet::Peer::kService:
        status = HandleSvcPacket(packet);
        break;
    case Packet::Peer::kEthernet:
        status = HandleDataPacket(packet);
        break;
    case Packet::Peer::kWlan: {
        auto fc = packet->field<FrameControl>(0);
        debugf("FrameControl type: %u subtype: %u\n", fc->type(), fc->subtype());
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
            HandleScanStatus(scanner_->HandleTimeout());
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
    return MX_OK;
}

mx_status_t Mlme::HandleMgmtPacket(const Packet* packet) {
    debugfn();
    auto hdr = packet->field<MgmtFrameHeader>(0);
    debugf("Frame control: %04x  duration: %u  seq: %u frag: %u\n",
            hdr->fc.val(), hdr->duration, hdr->sc.seq(), hdr->sc.frag());
    debugf("dest: " MAC_ADDR_FMT "  source: " MAC_ADDR_FMT "  bssid: " MAC_ADDR_FMT "\n",
            MAC_ADDR_ARGS(hdr->addr1),
            MAC_ADDR_ARGS(hdr->addr2),
            MAC_ADDR_ARGS(hdr->addr3));

    switch (hdr->fc.subtype()) {
    case ManagementSubtype::kBeacon:
        return HandleBeacon(packet);
    case ManagementSubtype::kProbeResponse:
        return HandleProbeResponse(packet);
    default:
        break;
    }
    return MX_OK;
}

mx_status_t Mlme::HandleSvcPacket(const Packet* packet) {
    debugfn();
    const uint8_t* p = packet->data();
    auto h = FromBytes<Header>(p, packet->len());
    debugf("service packet txn_id=%" PRIu64 " flags=%u ordinal=%u\n",
           h->txn_id, h->flags, h->ordinal);

    mx_status_t status = MX_OK;
    switch (static_cast<Method>(h->ordinal)) {
    case Method::SCAN_request:
        status = StartScan(packet);
        if (status != MX_OK) {
            errorf("could not start scan: %d\n", status);
        }
        break;
    default:
        warnf("unknown MLME method %u\n", h->ordinal);
        status = MX_ERR_NOT_SUPPORTED;
    }

    return status;
}

mx_status_t Mlme::HandleBeacon(const Packet* packet) {
    debugfn();

    if (scanner_->IsRunning()) {
        HandleScanStatus(scanner_->HandleBeacon(packet));
    }

    return MX_OK;
}

mx_status_t Mlme::HandleProbeResponse(const Packet* packet) {
    debugfn();

    if (scanner_->IsRunning()) {
        HandleScanStatus(scanner_->HandleProbeResponse(packet));
    }

    return MX_OK;
}

mx_status_t Mlme::StartScan(const Packet* packet) {
    debugfn();
    const uint8_t* p = packet->data();
    auto h = FromBytes<Header>(p, packet->len());
    MX_DEBUG_ASSERT(static_cast<Method>(h->ordinal) == Method::SCAN_request);

    auto req = ScanRequest::New();
    auto resp = ScanResponse::New();
    auto reqptr = reinterpret_cast<const void*>(h->payload);
    if (!req->Deserialize(const_cast<void*>(reqptr), packet->len() - h->len)) {
        warnf("could not deserialize ScanRequest\n");
        return MX_ERR_IO;
    }
    mx_status_t status = scanner_->Start(std::move(req), std::move(resp));
    if (status != MX_OK) {
        SendScanResponse();
        return status;
    }

    auto scan_chan = scanner_->ScanChannel();
    status = wlanmac_proxy_.SetChannel(0u, &scan_chan);
    if (status != MX_OK) {
        errorf("could not set channel to %u: %d\n", scan_chan.channel_num, status);
        SendScanResponse();
        scanner_->Reset();
    }
    return status;
}

void Mlme::HandleScanStatus(Scanner::Status status) {
    switch (status) {
    case Scanner::Status::kStartActiveScan:
        // TODO(tkilbourn): start sending probe requests
        break;
    case Scanner::Status::kNextChannel: {
        debugf("scan status: NextChannel\n");
        auto scan_chan = scanner_->ScanChannel();
        mx_status_t status = wlanmac_proxy_.SetChannel(0u, &scan_chan);
        if (status != MX_OK) {
            errorf("could not set channel to %u: %d\n", scan_chan.channel_num, status);
            scanner_->Reset();
            auto reset_status = wlanmac_proxy_.SetChannel(0u, &active_channel_);
            if (reset_status != MX_OK) {
                errorf("could not reset to active channel %u: %d\n",
                        active_channel_.channel_num, status);
                // TODO(tkilbourn): reset hw?
            }
        }
        break;
    }
    case Scanner::Status::kFinishScan: {
        debugf("scan status: FinishScan\n");
        mx_status_t status = SendScanResponse();
        if (status != MX_OK && status != MX_ERR_PEER_CLOSED) {
            errorf("could not send scan response: %d\n", status);
        }
        if (active_channel_.channel_num > 0) {
            status = wlanmac_proxy_.SetChannel(0u, &active_channel_);
            if (status != MX_OK) {
                errorf("could not reset to active channel %u: %d\n",
                        active_channel_.channel_num, status);
                // TODO(tkilbourn): reset hw?
            }
        }
        scanner_->Reset();
        break;
    }
    default:
        break;
    }
}

mx_status_t Mlme::SendScanResponse() {
    auto resp = scanner_->ScanResults();
    size_t buf_len = sizeof(Header) + resp->GetSerializedSize();
    mxtl::unique_ptr<uint8_t[]> buf(new uint8_t[buf_len]);
    auto header = FromBytes<Header>(buf.get(), buf_len);
    header->len = sizeof(Header);
    header->txn_id = 1;  // TODO(tkilbourn): txn ids
    header->flags = 0;
    header->ordinal = static_cast<uint32_t>(Method::SCAN_confirm);
    mx_status_t status = MX_OK;
    if (!resp->Serialize(header->payload, buf_len - sizeof(Header))) {
        errorf("could not serialize scan response\n");
        status = MX_ERR_IO;
    } else {
        status = mx_channel_write(service_, 0u, buf.get(), buf_len, NULL, 0);
        if (status != MX_OK) {
            errorf("could not send scan response: %d\n", status);
        }
    }
    return status;
}

}  // namespace wlan
