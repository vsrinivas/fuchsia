// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scanner.h"

#include "device_interface.h"
#include "element.h"
#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "serialize.h"
#include "timer.h"
#include "wlan.h"

#include <magenta/assert.h>
#include <mx/time.h>

#include <cinttypes>
#include <utility>

namespace wlan {

Scanner::Scanner(DeviceInterface* device, mxtl::unique_ptr<Timer> timer)
  : device_(device), timer_(std::move(timer)) {
    MX_DEBUG_ASSERT(timer_.get());
}

mx_status_t Scanner::Start(ScanRequestPtr req, ScanResponsePtr resp) {
    debugfn();
    resp_ = std::move(resp);
    resp_->bss_description_set = fidl::Array<BSSDescriptionPtr>::New(0);
    resp_->result_code = ResultCodes::NOT_SUPPORTED;

    if (IsRunning()) {
        return MX_ERR_UNAVAILABLE;
    }
    MX_DEBUG_ASSERT(req_.is_null());
    MX_DEBUG_ASSERT(channel_index_ == 0);
    MX_DEBUG_ASSERT(channel_start_ == 0);

    if (req->channel_list.size() == 0) {
        return MX_ERR_INVALID_ARGS;
    }
    if (req->max_channel_time < req->min_channel_time) {
        return MX_ERR_INVALID_ARGS;
    }
    if (!BSSTypes_IsValidValue(req->bss_type) || !ScanTypes_IsValidValue(req->scan_type)) {
        return MX_ERR_INVALID_ARGS;
    }

    // TODO(tkilbourn): define another result code (out of spec) for errors that aren't
    // NOT_SUPPORTED errors. Then set SUCCESS only when we've successfully finished scanning.
    resp_->result_code = ResultCodes::SUCCESS;
    req_ = std::move(req);

    channel_start_ = timer_->Now();
    mx_time_t timeout;
    if (req_->scan_type == ScanTypes::PASSIVE) {
        timeout = channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        timeout = channel_start_ + WLAN_TU(req_->probe_delay);
    }
    mx_status_t status = timer_->StartTimer(timeout);
    if (status != MX_OK) {
        errorf("could not start scan timer: %d\n", status);
    }

    return status;
}

mx_status_t Scanner::Start(ScanRequestPtr req) {
    debugfn();
    if (IsRunning()) {
        return MX_ERR_UNAVAILABLE;
    }
    MX_DEBUG_ASSERT(req_.is_null());
    MX_DEBUG_ASSERT(channel_index_ == 0);
    MX_DEBUG_ASSERT(channel_start_ == 0);

    resp_ = ScanResponse::New();
    resp_->bss_description_set = fidl::Array<BSSDescriptionPtr>::New(0);
    resp_->result_code = ResultCodes::NOT_SUPPORTED;

    if (req->channel_list.size() == 0) {
        return SendScanResponse();
    }
    if (req->max_channel_time < req->min_channel_time) {
        return SendScanResponse();
    }
    if (!BSSTypes_IsValidValue(req->bss_type) || !ScanTypes_IsValidValue(req->scan_type)) {
        return SendScanResponse();
    }

    // TODO(tkilbourn): define another result code (out of spec) for errors that aren't
    // NOT_SUPPORTED errors. Then set SUCCESS only when we've successfully finished scanning.
    resp_->result_code = ResultCodes::SUCCESS;
    req_ = std::move(req);

    channel_start_ = timer_->Now();
    mx_time_t timeout = InitialTimeout();
    mx_status_t status = device_->SetChannel(ScanChannel());
    if (status != MX_OK) {
        errorf("could not queue set channel: %d\n", status);
        SendScanResponse();
        Reset();
        return status;
    }

    status = timer_->StartTimer(timeout);
    if (status != MX_OK) {
        errorf("could not start scan timer: %d\n", status);
        resp_->result_code = ResultCodes::NOT_SUPPORTED;
        SendScanResponse();
        Reset();
        return status;
    }

    return MX_OK;
}

void Scanner::Reset() {
    debugfn();
    req_.reset();
    resp_.reset();
    channel_index_ = 0;
    channel_start_ = 0;
    timer_->CancelTimer();
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

mx_status_t Scanner::HandleBeacon(const Packet* packet) {
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    MX_DEBUG_ASSERT(rxinfo);
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
    bss->channel = rxinfo->chan.channel_num;
    if (rxinfo->flags & WLAN_RX_INFO_RSSI_PRESENT) {
        bss->rssi_measurement = rxinfo->rssi;
    } else {
        bss->rssi_measurement = 0xff;
    }
    if (rxinfo->flags & WLAN_RX_INFO_RCPI_PRESENT) {
        bss->rcpi_measurement = rxinfo->rcpi;
    } else {
        bss->rcpi_measurement = 0xff;
    }
    if (rxinfo->flags & WLAN_RX_INFO_SNR_PRESENT) {
        bss->rsni_measurement = rxinfo->snr;
    } else {
        bss->rsni_measurement = 0xff;
    }

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

    return MX_OK;
}

mx_status_t Scanner::HandleProbeResponse(const Packet* packet) {
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

    return MX_OK;
}

mx_status_t Scanner::HandleTimeout() {
    debugfn();
    MX_DEBUG_ASSERT(IsRunning());

    mx_time_t now = timer_->Now();
    mx_status_t status = MX_OK;

    // Reached max channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->max_channel_time)) {
        debugf("reached max channel time\n");
        if (++channel_index_ >= req_->channel_list.size()) {
            timer_->CancelTimer();
            status = SendScanResponse();
            Reset();
            return status;
        } else {
            channel_start_ = timer_->Now();
            status = timer_->StartTimer(InitialTimeout());
            if (status != MX_OK) {
                goto timer_fail;
            }
            return device_->SetChannel(ScanChannel());
        }
    }

    // TODO(tkilbourn): can probe delay come after min_channel_time?

    // Reached min channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->min_channel_time)) {
        debugf("Reached min channel time\n");
        // TODO(tkilbourn): if there was no sign of activity on this channel, skip ahead to the next
        // one
        // For now, just continue the scan.
        mx_time_t timeout = channel_start_ + WLAN_TU(req_->max_channel_time);
        status = timer_->StartTimer(timeout);
        if (status != MX_OK) {
            goto timer_fail;
        }
        return MX_OK;
    }

    // Reached probe delay for an active scan
    if (req_->scan_type == ScanTypes::ACTIVE &&
        now >= channel_start_ + WLAN_TU(req_->probe_delay)) {
        debugf("Reached probe delay\n");
        mx_time_t timeout = channel_start_ + WLAN_TU(req_->min_channel_time);
        status = timer_->StartTimer(timeout);
        if (status != MX_OK) {
            goto timer_fail;
        }
        // TODO(tkilbourn): send probe requests
        return MX_OK;
    }

    // Haven't reached a timeout yet; continue scanning
    return MX_OK;

timer_fail:
    errorf("could not set scan timer: %d\n", status);
    status = SendScanResponse();
    Reset();
    return status;
}

mx_status_t Scanner::HandleError(mx_status_t error_code) {
    debugfn();
    resp_ = ScanResponse::New();
    // TODO(tkilbourn): report the error code somehow
    resp_->result_code = ResultCodes::NOT_SUPPORTED;
    return SendScanResponse();
}

mx_time_t Scanner::InitialTimeout() const {
    if (req_->scan_type == ScanTypes::PASSIVE) {
        return channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        return channel_start_ + WLAN_TU(req_->probe_delay);
    }
}

mx_status_t Scanner::SendScanResponse() {
    debugfn();
    for (auto& bss : bss_descriptors_) {
        resp_->bss_description_set.push_back(std::move(bss.second));
    }

    size_t buf_len = sizeof(Header) + resp_->GetSerializedSize();
    mxtl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) {
        return MX_ERR_NO_RESOURCES;
    }

    auto packet = mxtl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    mx_status_t status = SerializeServiceMsg(packet.get(), Method::SCAN_confirm, resp_);
    if (status != MX_OK) {
        errorf("could not serialize ScanResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    bss_descriptors_.clear();
    return status;
}

}  // namespace wlan
