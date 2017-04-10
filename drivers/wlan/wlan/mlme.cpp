// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mlme.h"

#include "device.h"
#include "interface.h"
#include "logging.h"
#include "packet.h"

#include <apps/wlan/services/wlan_mlme.fidl-common.h>
#include <magenta/assert.h>
#include <mx/time.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

namespace wlan {

constexpr mx_duration_t kDefaultTimeout = MX_SEC(1);

Mlme::Mlme(ddk::WlanmacProtocolProxy wlanmac_proxy)
  : wlanmac_proxy_(wlanmac_proxy) {
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
    if (ethmac_proxy_ == nullptr) {
        warnf("ethmac not started\n");
    }
    ethmac_proxy_.reset();
}

void Mlme::GetDeviceInfo(ethmac_info_t* info) {
    debugfn();
    *info = ethmac_info_;
    // Make sure this device is reported as a wlan device
    info->features |= ETHMAC_FEATURE_WLAN;
}

namespace {
void DumpPacket(const Packet& packet) {
    const uint8_t* p = packet.Data();
    for (size_t i = 0; i < packet.Len(); i++) {
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
    MX_DEBUG_ASSERT(packet->Src() != Packet::Source::kUnknown);
    debugf("packet data=%p len=%zu src=%s\n",
            packet->Data(), packet->Len(),
            packet->Src() == Packet::Source::kWlan ? "Wlan" :
            packet->Src() == Packet::Source::kEthernet ? "Ethernet" :
            packet->Src() == Packet::Source::kService ? "Service" : "Unknown");

    if (kLogLevel >= kLogDebug) {
        DumpPacket(*packet);
    }

    switch (packet->Src()) {
    case Packet::Source::kService:
        return HandleSvcPacket(packet, next_timeout);
    case Packet::Source::kEthernet:
        return HandleDataPacket(packet, next_timeout);
    case Packet::Source::kWlan: {
        // TODO(tkilbourn): breakdown ctrl vs mgmt vs data frames
        break;
    }
    default:
        break;
    }
    *next_timeout = mx::deadline_after(kDefaultTimeout);
    return NO_ERROR;
}

mx_status_t Mlme::HandleTimeout(mx_time_t* next_timeout) {
    MX_DEBUG_ASSERT(next_timeout != nullptr);

    *next_timeout = mx::deadline_after(kDefaultTimeout);
    return NO_ERROR;
}

mx_status_t Mlme::HandleCtrlPacket(const Packet* packet, mx_time_t* next_timeout) {
    debugfn();
    *next_timeout = mx::deadline_after(kDefaultTimeout);
    return NO_ERROR;
}

mx_status_t Mlme::HandleDataPacket(const Packet* packet, mx_time_t* next_timeout) {
    debugfn();
    *next_timeout = mx::deadline_after(kDefaultTimeout);
    return NO_ERROR;
}

mx_status_t Mlme::HandleMgmtPacket(const Packet* packet, mx_time_t* next_timeout) {
    debugfn();
    *next_timeout = mx::deadline_after(kDefaultTimeout);
    return NO_ERROR;
}

mx_status_t Mlme::HandleSvcPacket(const Packet* packet, mx_time_t* next_timeout) {
    debugfn();
    if (sizeof(Header) > packet->Len()) {
        return ERR_IO_DATA_INTEGRITY;
    }

    const uint8_t* p = packet->Data();
    auto h = reinterpret_cast<const Header*>(p);
    debugf("service packet txn_id=%" PRIu64 " flags=%u ordinal=%u\n",
           h->txn_id, h->flags, h->ordinal);

    mx_status_t status = NO_ERROR;
    switch (static_cast<Method>(h->ordinal)) {
    case Method::SCAN: {
        ScanRequest req;
        if (h->len > packet->Len()) {
            return ERR_IO_DATA_INTEGRITY;
        }
        // TODO(tkilbourn): remove this const_cast when we have better FIDL deserialization
        auto reqptr = reinterpret_cast<void*>(const_cast<uint8_t*>(p) + h->len);
        if (!req.Deserialize(reqptr, packet->Len() - h->len)) {
            warnf("could not deserialize ScanRequest\n");
        } else {
            std::stringstream ss;
            ss << "ScanRequest BSS: " << req.bss_type << " ssid: " << req.ssid <<
                " ScanType: " << req.scan_type << " probe delay: " << req.probe_delay;
            infof("%s\n", ss.str().c_str());
        }
        break;
    }
    default:
        warnf("unknown MLME method %u\n", h->ordinal);
        status = ERR_NOT_SUPPORTED;
    }

    *next_timeout = mx::deadline_after(kDefaultTimeout);
    return status;
}

}  // namespace wlan
