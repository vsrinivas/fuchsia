// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mlme.h"

#include "device.h"
#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <mx/time.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

#define MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDR_ARGS(a) ((a)[0]), ((a)[1]), ((a)[2]), ((a)[3]), ((a)[4]), ((a)[5])

namespace wlan {

constexpr mx_duration_t kDefaultTimeout = MX_SEC(1);

Mlme::Mlme(ddk::WlanmacProtocolProxy wlanmac_proxy)
  : wlanmac_proxy_(wlanmac_proxy),
    scanner_(&clock_) {
    debugfn();
}

mx_status_t Mlme::Init() {
    debugfn();
    return wlanmac_proxy_.Query(0, &ethmac_info_);
}

mx_status_t Mlme::Start(mxtl::unique_ptr<ddk::EthmacIfcProxy> ethmac, Device* device) {
    debugfn();
    if (ethmac_proxy_ != nullptr) {
        return ERR_ALREADY_BOUND;
    }
    mx_status_t status = wlanmac_proxy_.Start(device);
    if (status != NO_ERROR) {
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

mx_status_t Mlme::HandlePacket(const Packet* packet, mx_time_t* next_timeout) {
    debugfn();
    MX_DEBUG_ASSERT(next_timeout != nullptr);
    MX_DEBUG_ASSERT(packet != nullptr);
    MX_DEBUG_ASSERT(packet->src() != Packet::Source::kUnknown);
    debugf("packet data=%p len=%zu src=%s\n",
            packet->data(), packet->len(),
            packet->src() == Packet::Source::kWlan ? "Wlan" :
            packet->src() == Packet::Source::kEthernet ? "Ethernet" :
            packet->src() == Packet::Source::kService ? "Service" : "Unknown");

    if (kLogLevel >= kLogDebug) {
        DumpPacket(*packet);
    }

    mx_status_t status = NO_ERROR;
    switch (packet->src()) {
    case Packet::Source::kService:
        status = HandleSvcPacket(packet);
        break;
    case Packet::Source::kEthernet:
        status = HandleDataPacket(packet);
        break;
    case Packet::Source::kWlan: {
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
            status = ERR_NOT_SUPPORTED;
            break;
        }
        break;
    }
    default:
        break;
    }

    SetNextTimeout(next_timeout);
    return status;
}

mx_status_t Mlme::HandleTimeout(mx_time_t* next_timeout) {
    MX_DEBUG_ASSERT(next_timeout != nullptr);

    mx_time_t now = clock_.Now();

    HandleScanTimeout(now);

    SetNextTimeout(next_timeout);
    return NO_ERROR;
}

mx_status_t Mlme::HandleCtrlPacket(const Packet* packet) {
    debugfn();
    return NO_ERROR;
}

mx_status_t Mlme::HandleDataPacket(const Packet* packet) {
    debugfn();
    return NO_ERROR;
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
    return NO_ERROR;
}

mx_status_t Mlme::HandleSvcPacket(const Packet* packet) {
    debugfn();
    const uint8_t* p = packet->data();
    auto h = FromBytes<Header>(p, packet->len());
    debugf("service packet txn_id=%" PRIu64 " flags=%u ordinal=%u\n",
           h->txn_id, h->flags, h->ordinal);

    mx_status_t status = NO_ERROR;
    switch (static_cast<Method>(h->ordinal)) {
    case Method::SCAN_request:
        status = StartScan(packet);
        if (status != NO_ERROR) {
            errorf("could not start scan: %d\n", status);
        }
        break;
    default:
        warnf("unknown MLME method %u\n", h->ordinal);
        status = ERR_NOT_SUPPORTED;
    }

    return status;
}

mx_status_t Mlme::HandleBeacon(const Packet* packet) {
    debugfn();

    if (scanner_.IsRunning()) {
        HandleScanStatus(scanner_.HandleBeacon(packet));
    }

    return NO_ERROR;
}

mx_status_t Mlme::HandleProbeResponse(const Packet* packet) {
    debugfn();

    if (scanner_.IsRunning()) {
        HandleScanStatus(scanner_.HandleProbeResponse(packet));
    }

    return NO_ERROR;
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
        return ERR_IO;
    }
    mx_status_t status = scanner_.Start(std::move(req), std::move(resp));
    if (status != NO_ERROR) {
        SendScanResponse();
        return status;
    }

    auto scan_chan = scanner_.ScanChannel();
    status = wlanmac_proxy_.SetChannel(0u, &scan_chan);
    if (status != NO_ERROR) {
        errorf("could not set channel to %u: %d\n", scan_chan.channel_num, status);
        SendScanResponse();
        scanner_.Reset();
    }
    return status;
}

void Mlme::HandleScanTimeout(mx_time_t now) {
    auto scan_timeout = scanner_.NextTimeout();
    if (scan_timeout > 0 && scan_timeout < now) {
        HandleScanStatus(scanner_.HandleTimeout(now));
    }
}

void Mlme::HandleScanStatus(Scanner::Status status) {
    switch (status) {
    case Scanner::Status::kStartActiveScan:
        // TODO(tkilbourn): start sending probe requests
        break;
    case Scanner::Status::kNextChannel: {
        debugf("scan status: NextChannel\n");
        auto scan_chan = scanner_.ScanChannel();
        mx_status_t status = wlanmac_proxy_.SetChannel(0u, &scan_chan);
        if (status != NO_ERROR) {
            errorf("could not set channel to %u: %d\n", scan_chan.channel_num, status);
            scanner_.Reset();
            auto reset_status = wlanmac_proxy_.SetChannel(0u, &active_channel_);
            if (reset_status != NO_ERROR) {
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
        if (status != NO_ERROR && status != ERR_PEER_CLOSED) {
            errorf("could not send scan response: %d\n", status);
        }
        if (active_channel_.channel_num > 0) {
            status = wlanmac_proxy_.SetChannel(0u, &active_channel_);
            if (status != NO_ERROR) {
                errorf("could not reset to active channel %u: %d\n",
                        active_channel_.channel_num, status);
                // TODO(tkilbourn): reset hw?
            }
        }
        scanner_.Reset();
        break;
    }
    default:
        break;
    }
}

mx_status_t Mlme::SendScanResponse() {
    auto resp = scanner_.ScanResults();
    size_t buf_len = sizeof(Header) + resp->GetSerializedSize();
    mxtl::unique_ptr<uint8_t[]> buf(new uint8_t[buf_len]);
    auto header = FromBytes<Header>(buf.get(), buf_len);
    header->len = sizeof(Header);
    header->txn_id = 1;  // TODO(tkilbourn): txn ids
    header->flags = 0;
    header->ordinal = static_cast<uint32_t>(Method::SCAN_confirm);
    mx_status_t status = NO_ERROR;
    if (!resp->Serialize(header->payload, buf_len - sizeof(Header))) {
        errorf("could not serialize scan response\n");
        status = ERR_IO;
    } else {
        status = mx_channel_write(service_, 0u, buf.get(), buf_len, NULL, 0);
        if (status != NO_ERROR) {
            errorf("could not send scan response: %d\n", status);
        }
    }
    return status;
}

void Mlme::SetNextTimeout(mx_time_t* next_timeout) {
    *next_timeout = mx::deadline_after(kDefaultTimeout);

    auto scan_timeout = scanner_.NextTimeout();
    if (scan_timeout > 0 && scan_timeout < *next_timeout) {
        *next_timeout = scan_timeout;
    }
}

}  // namespace wlan
