// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scanner.h"

#include "clock.h"
#include "element.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"

#include <magenta/assert.h>
#include <mx/time.h>

#include <cinttypes>
#include <utility>

namespace wlan {

namespace {
// Consider moving this to a utilities lib if we need it in more places
uint64_t MacToUint64(const uint8_t mac[6]) {
    uint64_t m = mac[0];
    for (int i = 1; i < 6; i++) {
        m <<= 8;
        m |= mac[i];
    }
    return m;
}
}  // namespace

Scanner::Scanner(Clock* clock)
  : clock_(clock) {
    MX_DEBUG_ASSERT(clock);
}

mx_status_t Scanner::Start(ScanRequestPtr req, ScanResponsePtr resp) {
    debugfn();
    resp_ = std::move(resp);
    resp_->bss_description_set = fidl::Array<BSSDescriptionPtr>::New(0);
    resp_->result_code = ResultCodes::NOT_SUPPORTED;

    if (IsRunning()) {
        return ERR_UNAVAILABLE;
    }
    MX_DEBUG_ASSERT(req_.is_null());
    MX_DEBUG_ASSERT(channel_index_ == 0);
    MX_DEBUG_ASSERT(channel_start_ == 0);

    if (req->channel_list.size() == 0) {
        return ERR_INVALID_ARGS;
    }
    if (req->max_channel_time < req->min_channel_time) {
        return ERR_INVALID_ARGS;
    }
    if (!BSSTypes_IsValidValue(req->bss_type) || !ScanTypes_IsValidValue(req->scan_type)) {
        return ERR_INVALID_ARGS;
    }

    // TODO(tkilbourn): define another result code (out of spec) for errors that aren't
    // NOT_SUPPORTED errors. Then set SUCCESS only when we've successfully finished scanning.
    resp_->result_code = ResultCodes::SUCCESS;
    req_ = std::move(req);

    channel_start_ = clock_->Now();
    if (req_->scan_type == ScanTypes::PASSIVE) {
        next_timeout_ = channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        next_timeout_ = channel_start_ + WLAN_TU(req_->probe_delay);
    }

    return NO_ERROR;
}

void Scanner::Reset() {
    debugfn();
    req_.reset();
    resp_.reset();
    channel_index_ = 0;
    channel_start_ = 0;
    next_timeout_ = 0;
    bss_descriptors_.clear();
}

bool Scanner::IsRunning() const {
    return !req_.is_null();
}

Scanner::Type Scanner::ScanType() const {
    MX_DEBUG_ASSERT(IsRunning());
    switch (req_->scan_type) {
    case ScanTypes::PASSIVE:
        return Type::kPassive;
    case ScanTypes::ACTIVE:
        return Type::kActive;
    }
}

wlan_channel_t Scanner::ScanChannel() const {
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());
    MX_DEBUG_ASSERT(channel_index_ < req_->channel_list.size());

    return wlan_channel_t{req_->channel_list[channel_index_]};
}

mx_time_t Scanner::NextTimeout() const {
    return next_timeout_;
}

Scanner::Status Scanner::HandleBeacon(const Packet* packet) {
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());

    auto hdr = packet->field<MgmtFrameHeader>(0);
    auto bcn = packet->field<Beacon>(hdr->size());
    debugf("timestamp: %" PRIu64 " beacon interval: %u capabilities: %04x\n",
            bcn->timestamp, bcn->beacon_interval, bcn->cap.val());

    BSSDescription* bss;
    uint64_t sender = MacToUint64(hdr->addr2);
    auto entry = bss_descriptors_.find(sender);
    if (entry == bss_descriptors_.end()) {
        auto bssptr = BSSDescription::New();
        bss = bssptr.get();
        bss_descriptors_.insert({sender, std::move(bssptr)});

        bss->bssid = fidl::Array<uint8_t>::New(sizeof(hdr->addr3));
        std::memcpy(bss->bssid.data(), hdr->addr3, bss->bssid.size());
    } else {
        bss = entry->second.get();
    }

    // Insert / update all the fields
    if (bcn->cap.ess()) {
        bss->bss_type = BSSTypes::INFRASTRUCTURE;
    } else if (bcn->cap.ibss()) {
        bss->bss_type = BSSTypes::INDEPENDENT;
    }
    bss->beacon_period = bcn->beacon_interval;
    bss->timestamp = bcn->timestamp;

    size_t elt_len = packet->len() - hdr->size() - sizeof(Beacon);
    ElementReader reader(bcn->elements, elt_len);

    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) break;

        switch (hdr->id) {
        case ElementId::kSsid: {
            auto ssid = reader.read<SsidElement>();
            debugf("ssid: %.*s\n", ssid->hdr.len, ssid->ssid);
            bss->ssid = fidl::String(ssid->ssid, ssid->hdr.len);
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

    return Status::kContinueScan;
}

Scanner::Status Scanner::HandleProbeResponse(const Packet* packet) {
    // TODO(tkilbourn): consolidate with HandleBeacon
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());

    auto hdr = packet->field<MgmtFrameHeader>(0);
    auto resp = packet->field<ProbeResponse>(hdr->size());
    debugf("timestamp: %" PRIu64 " beacon interval: %u capabilities: %04x\n",
            resp->timestamp, resp->beacon_interval, resp->cap.val());

    size_t elt_len = packet->len() - hdr->size() - sizeof(ProbeResponse);
    ElementReader reader(resp->elements, elt_len);

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
                bptr += snprintf(bptr, sizeof(buf) - (bptr - buf), " %u", supprates->rates[i]);
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

    return Status::kContinueScan;
}

Scanner::Status Scanner::HandleTimeout(mx_time_t now) {
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());

    // Reached max channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->max_channel_time)) {
        debugf("reached max channel time\n");
        if (++channel_index_ >= req_->channel_list.size()) {
            next_timeout_ = 0;
            return Status::kFinishScan;
        } else {
            channel_start_ = clock_->Now();
            if (req_->scan_type == ScanTypes::PASSIVE) {
                next_timeout_ = channel_start_ + WLAN_TU(req_->min_channel_time);
            } else {
                next_timeout_ = channel_start_ + WLAN_TU(req_->probe_delay);
            }
            return Status::kNextChannel;
        }
    }

    // TODO(tkilbourn): can probe delay come after min_channel_time?

    // Reached min channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->min_channel_time)) {
        debugf("Reached min channel time\n");
        // TODO(tkilbourn): if there was no sign of activity on this channel, skip ahead to the next
        // one
        // For now, just continue the scan.
        next_timeout_ = channel_start_ + WLAN_TU(req_->max_channel_time);
        return Status::kContinueScan;
    }

    // Reached probe delay for an active scan
    if (req_->scan_type == ScanTypes::ACTIVE &&
        now >= channel_start_ + WLAN_TU(req_->probe_delay)) {
        debugf("Reached probe delay\n");
        next_timeout_ = channel_start_ + WLAN_TU(req_->min_channel_time);
        return Status::kStartActiveScan;
    }

    // Haven't reached a timeout yet; continue scanning
    return Status::kContinueScan;
}

mx_status_t Scanner::FillProbeRequest(ProbeRequest* request, size_t len) const {
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());

    return ERR_NOT_SUPPORTED;
}

ScanResponsePtr Scanner::ScanResults() {
    for (auto& bss : bss_descriptors_) {
        resp_->bss_description_set.push_back(std::move(bss.second));
    }
    bss_descriptors_.clear();
    return std::move(resp_);
}

}  // namespace wlan
