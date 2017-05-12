// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "element.h"

namespace wlan {

ElementReader::ElementReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

bool ElementReader::is_valid() const {
    // offset_ + 1 is the length field of the next element.
    return offset_ + 1 < len_ && offset_ + NextElementLen() <= len_;
}

const ElementHeader* ElementReader::peek() const {
    if (!is_valid()) return nullptr;
    return reinterpret_cast<const ElementHeader*>(buf_ + offset_);
}

size_t ElementReader::NextElementLen() const {
    return sizeof(ElementHeader) + buf_[offset_ + 1];
}

ElementWriter::ElementWriter(uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

const size_t SsidElement::kMaxLen;

bool SsidElement::Create(uint8_t* buf, size_t len, size_t* actual, const char* ssid) {
    size_t ssidlen = 0;
    if (ssid != nullptr) {
        ssidlen = strnlen(ssid, kMaxLen + 1);
    }
    if (ssidlen == kMaxLen + 1) return false;
    size_t elem_size = sizeof(SsidElement) + ssidlen;
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<SsidElement*>(buf);
    elem->hdr.id = kSsid;
    elem->hdr.len = ssidlen;
    std::memcpy(elem->ssid, ssid, ssidlen);
    *actual = elem_size;
    return true;
}

const size_t SupportedRatesElement::kMaxLen;

bool SupportedRatesElement::Create(uint8_t* buf, size_t len, size_t* actual,
                                   const std::vector<uint8_t>& rates) {
    if (rates.size() > kMaxLen) return false;
    size_t elem_size = sizeof(SupportedRatesElement) + rates.size();
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<SupportedRatesElement*>(buf);
    elem->hdr.id = kSuppRates;
    elem->hdr.len = rates.size();
    std::copy(rates.begin(), rates.end(), elem->rates);
    *actual = elem_size;
    return true;
}

bool DsssParamSetElement::Create(uint8_t* buf, size_t len, size_t* actual, uint8_t chan) {
    size_t elem_size = sizeof(DsssParamSetElement);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<DsssParamSetElement*>(buf);
    elem->hdr.id = kDsssParamSet;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    elem->current_chan = chan;
    *actual = elem_size;
    return true;
}

bool CfParamSetElement::Create(uint8_t* buf, size_t len, size_t* actual, uint8_t count,
                               uint8_t period, uint16_t max_duration, uint16_t dur_remaining) {
    size_t elem_size = sizeof(CfParamSetElement);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<CfParamSetElement*>(buf);
    elem->hdr.id = kCfParamSet;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    elem->count = count;
    elem->period = period;
    elem->max_duration = max_duration;
    elem->dur_remaining = dur_remaining;
    *actual = elem_size;
    return true;
}

bool CountryElement::Create(uint8_t* buf, size_t len, size_t* actual, const char* country) {
    size_t elem_size = sizeof(CountryElement);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<CountryElement*>(buf);
    elem->hdr.id = kCountry;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    std::strncpy(elem->country, country, sizeof(elem->country));
    *actual = elem_size;
    return true;
}

}  // namespace wlan
