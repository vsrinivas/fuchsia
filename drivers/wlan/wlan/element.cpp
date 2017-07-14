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
    if (aid >= kMaxLen * 8 || hdr.len < 4) return false;
    if (!bmp_ctrl.offset() && hdr.len == 4) return false;

    // Safe to use uint8 since offset is 7 bits.
    uint8_t n1 = bmp_ctrl.offset() << 1;
    uint16_t n2 = (hdr.len - 4) + n1;
    if (n2 > static_cast<uint16_t> (kMaxLen)) return false;

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


bool RsnElement::Create(uint8_t* buf, size_t len, size_t* actual, uint16_t version,
                        CipherSuite* group_data_cipher_suite,
                        const std::vector<CipherSuite>& pairwise_cipher_suite,
                        const std::vector<AkmSuite>& akm_suite,
                        RsnCapabilities* rsn_cap,
                        const std::vector<__uint128_t>& pmkids,
                        CipherSuite* group_mgmt_cipher_suite) {
    uint16_t max_suite_entries = 255;

    size_t elem_size = sizeof(RsnElement);
    if (!group_data_cipher_suite) {
        goto contd;
    }
    elem_size += sizeof(CipherSuite);

    if  (pairwise_cipher_suite.empty()) {
        goto contd;
    }
    if (pairwise_cipher_suite.size() > max_suite_entries) {
        return false;
    }
    elem_size += 2 + pairwise_cipher_suite.size() * sizeof(CipherSuite);

    if  (akm_suite.empty()) {
        goto contd;
    }
    if (akm_suite.size() > max_suite_entries) {
        return false;
    }
    elem_size += 2 + akm_suite.size() * sizeof(CipherSuite);

    if  (!rsn_cap) {
        goto contd;
    }
    elem_size += sizeof(RsnCapabilities);

    if  (pmkids.empty()) {
        goto contd;
    }
    if (pmkids.size() > max_suite_entries) {
        return false;
    }
    elem_size += 2 + pmkids.size() * sizeof(__uint128_t);

    if  (!group_mgmt_cipher_suite) goto contd;
    elem_size += sizeof(CipherSuite);

contd:
    if (elem_size > len) {
        return false;
    }
    auto elem = reinterpret_cast<RsnElement*>(buf);
    elem->hdr.id = element_id::kRsn;
    elem->hdr.len = elem_size - sizeof(ElementHeader);
    elem->version = version;

    if (!group_data_cipher_suite) {
        goto done;
    } else {
        *elem->group_data_cipher_suite() = *group_data_cipher_suite;
    }

    if  (pairwise_cipher_suite.empty()) {
        goto done;
    } else {
        auto pairwise = elem->pairwise_cipher_suite();
        pairwise->count = pairwise_cipher_suite.size();
        std::copy(pairwise_cipher_suite.begin(), pairwise_cipher_suite.end(), pairwise->list);
    }

    if  (akm_suite.empty()) {
        goto done;
    } else {
        auto akm = elem->akm_suite();
        akm->count = akm_suite.size();
        std::copy(akm_suite.begin(), akm_suite.end(), akm->list);
    }

    if  (!rsn_cap) {
        goto done;
    } else {
        elem->rsn_cap()->set_val(rsn_cap->val());
    }

    if  (pmkids.empty()) {
        goto done;
    } else {
        auto pmkid = elem->pmkid();
        pmkid->count = pmkids.size();
        std::copy(pmkids.begin(), pmkids.end(), pmkid->list);
    }

    if  (!group_mgmt_cipher_suite) {
        goto done;
    } else {
        *elem->group_mgmt_cipher_suite() = *group_mgmt_cipher_suite;
    }

done:
    *actual = elem_size;
    return true;
}

namespace {

template <typename T>
size_t size(T* t) {
    return sizeof(T);
}

template <typename U>
size_t size(RsnOptionalList<U>* list) {
    return list->size();
}

} // namespace

template <typename T, typename U>
T* RsnElement::next_optional(const U* previous, size_t len) const {
  if (hdr.len < sizeof(version)) return nullptr;
  if (!previous) return nullptr;

  // First check if the object fits into the header.
  auto start = (uint8_t*)previous + len;
  auto end = fields + hdr.len - sizeof(version);
  if (start + sizeof(T) > end) return nullptr;

  // Then, resolve the object and check whether it also
  // fits into the header if it's of dynamic size.
  auto obj = reinterpret_cast<T*>(start);
  if (start + size(obj) > end) return nullptr;

  return obj;
}

}  // namespace wlan
