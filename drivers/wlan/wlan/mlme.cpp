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
#include <mx/time.h>

#include <cinttypes>
#include <cstring>
#include <sstream>

#define MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDR_ARGS(a) ((a)[0]), ((a)[1]), ((a)[2]), ((a)[3]), ((a)[4]), ((a)[5])

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
    case ManagementSubtype::kBeacon: {
        auto bcn = packet->field<Beacon>(hdr->size());
        debugf("timestamp: %" PRIu64 " beacon interval: %u capabilities: %04x\n",
                bcn->timestamp, bcn->beacon_interval, bcn->cap.val());

        size_t elt_len = packet->len() - hdr->size() - sizeof(Beacon);
        ElementReader reader(bcn->elements, elt_len);

        while (reader.is_valid()) {
            const ElementHeader* hdr = reader.peek();
            if (hdr == nullptr) break;

            switch (hdr->id) {
            case ElementId::kSsid: {
                auto ssid = reader.read<SsidElement>();
                debugf("ssid: %.*s\n", ssid->hdr.len, ssid->ssid);
                break;
            }
            case ElementId::kSuppRates: {
                auto supprates = reader.read<SupportedRatesElement>();
                if (supprates == nullptr) goto done_iter;
                char buf[256];
                char* bptr = buf;
                for (int i = 0; i < supprates->hdr.len; i++) {
                    size_t used = bptr - buf;
                    MX_DEBUG_ASSERT(sizeof(buf) > used);
                    bptr += snprintf(bptr, sizeof(buf) - used, " %u", supprates->rates[i]);
                }
                debugf("supported rates:%s\n", buf);
                break;
            }
            case ElementId::kDsssParamSet: {
                auto dsss_params = reader.read<DsssParamSetElement>();
                if (dsss_params == nullptr) goto done_iter;
                debugf("current channel: %u\n", dsss_params->current_chan);
                break;
            }
            case ElementId::kCountry: {
                auto country = reader.read<CountryElement>();
                if (country == nullptr) goto done_iter;
                debugf("country: %.*s\n", 3, country->country);
                break;
            }
            default:
                debugf("unknown element id: %u len: %u\n", hdr->id, hdr->len);
                reader.skip(sizeof(ElementHeader) + hdr->len);
                break;
            }
        }
done_iter:
        break;
    }
    default:
        break;
    }
    return NO_ERROR;
}

mx_status_t Mlme::HandleSvcPacket(const Packet* packet) {
    debugfn();
    auto* p = packet->data();
    auto h = reinterpret_cast<const Header*>(p);
    debugf("service packet txn_id=%" PRIu64 " flags=%u ordinal=%u\n",
           h->txn_id, h->flags, h->ordinal);

    mx_status_t status = NO_ERROR;
    switch (static_cast<Method>(h->ordinal)) {
    case Method::SCAN: {
        ScanRequest req;
        if (h->len > packet->len()) {
            return ERR_IO_DATA_INTEGRITY;
        }
        // TODO(tkilbourn): remove this const_cast when we have better FIDL deserialization
        auto reqptr = reinterpret_cast<void*>(const_cast<uint8_t*>(p) + h->len);
        if (!req.Deserialize(reqptr, packet->len() - h->len)) {
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

    return status;
}

void Mlme::SetNextTimeout(mx_time_t* next_timeout) {
    *next_timeout = mx::deadline_after(kDefaultTimeout);
}

}  // namespace wlan
