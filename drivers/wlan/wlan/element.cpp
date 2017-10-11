// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "element.h"

namespace wlan {

ElementReader::ElementReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

bool ElementReader::is_valid() const {
    // Test in order.
    // Test 1. If at least ElementHeader can be safely read: 2 bytes test.
    if (len_ < offset_ + sizeof(ElementHeader)) return false;

    // Test 2. If the full element can be safely read.
    // NextElementLen() needs pass from Test 1.
    if (len_ < offset_ + NextElementLen()) return false;

    // Add more test here.
    return true;
}

const ElementHeader* ElementReader::peek() const {
    if (!is_valid()) return nullptr;
    return reinterpret_cast<const ElementHeader*>(buf_ + offset_);
}

size_t ElementReader::NextElementLen() const {
    auto hdr = reinterpret_cast<const ElementHeader*>(buf_ + offset_);
    return sizeof(ElementHeader) + hdr->len;
}

ElementWriter::ElementWriter(uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

const size_t SsidElement::kMaxLen;

bool SsidElement::Create(uint8_t* buf, size_t len, size_t* actual, const char* ssid) {
    size_t ssidlen = 0;
    if (ssid != nullptr) { ssidlen = strnlen(ssid, kMaxLen + 1); }
    if (ssidlen == kMaxLen + 1) return false;
    size_t elem_size = sizeof(SsidElement) + ssidlen;
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<SsidElement*>(buf);
    elem->hdr.id = element_id::kSsid;
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
    elem->hdr.id = element_id::kSuppRates;
    elem->hdr.len = rates.size();
    std::copy(rates.begin(), rates.end(), elem->rates);
    *actual = elem_size;
    return true;
}

bool DsssParamSetElement::Create(uint8_t* buf, size_t len, size_t* actual, uint8_t chan) {
    size_t elem_size = sizeof(DsssParamSetElement);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<DsssParamSetElement*>(buf);
    elem->hdr.id = element_id::kDsssParamSet;
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
    elem->hdr.id = element_id::kCfParamSet;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    elem->count = count;
    elem->period = period;
    elem->max_duration = max_duration;
    elem->dur_remaining = dur_remaining;
    *actual = elem_size;
    return true;
}

bool TimElement::Create(uint8_t* buf, size_t len, size_t* actual, uint8_t dtim_count,
                        uint8_t dtim_period, BitmapControl bmp_ctrl,
                        const std::vector<uint8_t>& bmp) {
    if (bmp.size() > kMaxLen) return false;
    size_t elem_size = sizeof(TimElement) + bmp.size();
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<TimElement*>(buf);
    elem->hdr.id = element_id::kTim;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    elem->dtim_count = dtim_count;
    elem->dtim_period = dtim_period;
    elem->bmp_ctrl = bmp_ctrl;
    std::copy(bmp.begin(), bmp.end(), elem->bmp);
    *actual = elem_size;
    return true;
}

// TODO(hahnr): Support dot11MultiBSSIDActivated is true.
bool TimElement::traffic_buffered(uint16_t aid) const {
    // Illegal arguments or no partial virtual bitmap. No traffic buffered.
    if (aid >= kMaxLenBmp * 8 || hdr.len < kMinLen) return false;
    if (!bmp_ctrl.offset() && hdr.len == kMinLen) return false;

    // Safe to use uint8 since offset is 7 bits.
    uint8_t n1 = bmp_ctrl.offset() << 1;
    uint16_t n2 = (hdr.len - kMinLen) + n1;
    if (n2 > static_cast<uint16_t>(kMaxLenBmp)) return false;

    // No traffic buffered for aid.
    uint8_t octet = aid / 8;
    if (octet < n1 || octet > n2) return false;

    // Traffic might be buffered for aid
    // Bounds are not exceeded since (n2 - n1 + 4) = hdr.len, and
    // n1 <= octet <= n2, and hdr.len >= 4. This simplifies to:
    // 0 <=  octet - n1 <= (hdr.len - 4)
    return bmp[octet - n1] & (1 << (aid % 8));
}

bool CountryElement::Create(uint8_t* buf, size_t len, size_t* actual, const char* country) {
    size_t elem_size = sizeof(CountryElement);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<CountryElement*>(buf);
    elem->hdr.id = element_id::kCountry;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    std::strncpy(elem->country, country, sizeof(elem->country));
    *actual = elem_size;
    return true;
}

const size_t ExtendedSupportedRatesElement::kMaxLen;

bool ExtendedSupportedRatesElement::Create(uint8_t* buf, size_t len, size_t* actual,
                                           const std::vector<uint8_t>& rates) {
    if (rates.size() > kMaxLen) return false;
    size_t elem_size = sizeof(ExtendedSupportedRatesElement) + rates.size();
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<ExtendedSupportedRatesElement*>(buf);
    elem->hdr.id = element_id::kExtSuppRates;
    elem->hdr.len = rates.size();
    std::copy(rates.begin(), rates.end(), elem->rates);
    *actual = elem_size;
    return true;
}

bool RsnElement::Create(uint8_t* buf, size_t len, size_t* actual, uint8_t* raw, size_t raw_len) {
    if (raw_len < sizeof(RsnElement)) return false;
    if (raw_len > len) return false;

    auto elem = reinterpret_cast<RsnElement*>(buf);
    memcpy(elem, raw, raw_len);
    elem->hdr.id = element_id::kRsn;
    elem->hdr.len = raw_len - sizeof(ElementHeader);
    *actual = raw_len;
    return true;
}

bool HtCapabilities::Create(uint8_t* buf, size_t len, size_t* actual, HtCapabilityInfo ht_cap_info,
                            AmpduParams ampdu_params, SupportedMcsSet mcs_set,
                            HtExtCapabilities ht_ext_cap, TxBfCapability txbf_cap,
                            AselCapability asel_cap) {
    size_t elem_size = sizeof(HtCapabilities);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<HtCapabilities*>(buf);
    elem->hdr.id = element_id::kHtCapabilities;
    elem->hdr.len = elem_size - sizeof(ElementHeader);

    elem->ht_cap_info = ht_cap_info;
    elem->ampdu_params = ampdu_params;
    elem->mcs_set = mcs_set;
    elem->ht_ext_cap = ht_ext_cap;
    elem->txbf_cap = txbf_cap;
    elem->asel_cap = asel_cap;

    *actual = elem_size;
    return true;
}

bool HtOperation::Create(uint8_t* buf, size_t len, size_t* actual, uint8_t primary_chan,
                         HtOpInfoHead head, HtOpInfoTail tail, SupportedMcsSet mcs_set) {
    size_t elem_size = sizeof(HtOperation);
    if (elem_size > len) return false;

    auto elem = reinterpret_cast<HtOperation*>(buf);
    elem->hdr.id = element_id::kHtOperation;
    elem->hdr.len = elem_size - sizeof(ElementHeader);

    elem->primary_chan = primary_chan;
    elem->head = head;
    elem->tail = tail;
    elem->mcs_set = mcs_set;
    return true;
}

}  // namespace wlan
