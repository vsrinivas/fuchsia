// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "element_id.h"
#include "garnet/drivers/wlan/common/bitfield.h"
#include "logging.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wlan {

// IEEE Std 802.11-2016, 9.4.2.1
struct ElementHeader {
    uint8_t id;
    uint8_t len;
} __PACKED;

template <typename E, uint8_t ID> struct Element {
    static constexpr uint8_t element_id() { return ID; }
    size_t body_len() const { return static_cast<const E*>(this)->hdr.len; }
    size_t len() const { return sizeof(ElementHeader) + body_len(); }

    bool is_len_valid() const {
        // E::kMinLen and E::kMaxLen captures the range of the IE body length,
        // excluding the IE header whose size is fixed to 2 octets.
        if (body_len() >= E::kMinLen && body_len() <= E::kMaxLen) return true;

        // Crush dark arts.
        debugbcn("rxed Invalid IE: ID %2d elem_len %2zu body_len %3zu (not in range [%3zu:%3zu]\n",
                 E::element_id(), len(), body_len(), E::kMinLen, E::kMaxLen);
        return false;
    }

    bool is_valid() const { return is_len_valid(); }
};

// An ElementReader can be used to retrieve Elements from a Management frame. The peek() method
// will read the ElementHeader without advancing the iterator. The caller may then use the id in the
// header and call read() to retrieve the next Element. is_valid() will return false after reaching
// the end or parse errors. It is an error to call read() for a type different than the one
// specified in the ElementHeader.
class ElementReader {
   public:
    ElementReader(const uint8_t* buf, size_t len);

    bool is_valid() const;
    size_t offset() const { return offset_; }
    const ElementHeader* peek() const;
    void skip(size_t offset) { offset_ += offset; }
    void skip(const ElementHeader& hdr) { offset_ += sizeof(ElementHeader) + hdr.len; }

    template <typename E> const E* read() {
        static_assert(fbl::is_base_of<Element<E, E::element_id()>, E>::value,
                      "Only Elements may be retrieved.");
        if (!is_valid()) {
            debugbcn("IE validity test failed: ID %3u len_ %3zu offset_ %3zu elem_len %3zu\n",
                     E::element_id(), len_, offset_, NextElementLen());

            return nullptr;
        }

        if (offset_ + sizeof(E) > len_) {
            debugbcn(
                "IE validity test failed: ID %3u len_ %3zu offset_ %3zu elem_len %3zu sizeof(E) "
                "%3zu\n",
                E::element_id(), len_, offset_, NextElementLen(), sizeof(E));
            return nullptr;
        }

        const E* elt = reinterpret_cast<const E*>(buf_ + offset_);
        ZX_DEBUG_ASSERT(elt->hdr.id == E::element_id());
        if (!elt->is_valid()) return nullptr;

        offset_ += sizeof(ElementHeader) + elt->hdr.len;
        return elt;
    }

   private:
    size_t NextElementLen() const;

    const uint8_t* const buf_;
    const size_t len_;
    size_t offset_ = 0;
};

// An ElementWriter will serialize Elements into a buffer. The size() method will return the
// total length of the buffer.
class ElementWriter {
   public:
    ElementWriter(uint8_t* buf, size_t len);

    template <typename E, typename... Args> bool write(Args&&... args) {
        static_assert(fbl::is_base_of<Element<E, E::element_id()>, E>::value,
                      "Only Elements may be inserted.");
        if (offset_ >= len_) return false;

        size_t actual = 0;
        bool success =
            E::Create(buf_ + offset_, len_ - offset_, &actual, std::forward<Args>(args)...);
        if (!success) return false;

        auto elem = reinterpret_cast<const E*>(buf_ + offset_);
        if (!elem->is_valid()) {
            warnf("ElementWriter: IE %3u has invalid body length: %3u\n", E::element_id(),
                  elem->hdr.len);
        }

        offset_ += actual;
        ZX_DEBUG_ASSERT(offset_ <= len_);
        return true;
    }

    size_t size() const { return offset_; }

   private:
    uint8_t* buf_;
    const size_t len_;
    size_t offset_ = 0;
};

// IEEE Std 802.11-2016, 9.4.2.2
struct SsidElement : public Element<SsidElement, element_id::kSsid> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, const char* ssid);
    static const size_t kMinLen = 0;
    static const size_t kMaxLen = 32;

    ElementHeader hdr;
    char ssid[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.3
struct SupportedRatesElement : public Element<SupportedRatesElement, element_id::kSuppRates> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, const std::vector<uint8_t>& rates);
    static const size_t kMinLen = 1;
    static const size_t kMaxLen = 8;

    ElementHeader hdr;
    uint8_t rates[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.4
struct DsssParamSetElement : public Element<DsssParamSetElement, element_id::kDsssParamSet> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t chan);
    static const size_t kMinLen = 1;
    static const size_t kMaxLen = 1;

    ElementHeader hdr;
    uint8_t current_chan;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.5
struct CfParamSetElement : public Element<CfParamSetElement, element_id::kCfParamSet> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t count, uint8_t period,
                       uint16_t max_duration, uint16_t dur_remaining);
    static const size_t kMinLen = 6;
    static const size_t kMaxLen = 6;

    ElementHeader hdr;
    uint8_t count;
    uint8_t period;
    uint16_t max_duration;
    uint16_t dur_remaining;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.6
class BitmapControl : public common::BitField<uint8_t> {
   public:
    WLAN_BIT_FIELD(group_traffic_ind, 0, 1);
    WLAN_BIT_FIELD(offset, 1, 7);
};

// IEEE Std 802.11-2016, 9.4.2.6
struct TimElement : public Element<TimElement, element_id::kTim> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t dtim_count,
                       uint8_t dtim_period, BitmapControl bmp_ctrl,
                       const std::vector<uint8_t>& bmp);
    static const size_t kMinLenBmp = 1;
    static const size_t kMaxLenBmp = 251;
    static const size_t kFixedLenBody = 3;
    static const size_t kMinLen = kFixedLenBody + kMinLenBmp;
    static const size_t kMaxLen = kFixedLenBody + kMaxLenBmp;

    ElementHeader hdr;

    // body: fixed 3 bytes
    uint8_t dtim_count;
    uint8_t dtim_period;
    BitmapControl bmp_ctrl;

    // body: variable length 1-251 bytes.
    uint8_t bmp[];

    bool traffic_buffered(uint16_t aid) const;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.9
struct CountryElement : public Element<CountryElement, element_id::kCountry> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, const char* country);
    static const size_t kCountryLen = 3;
    static const size_t kMinLen = 3;  // TODO(porce): revisit the spec.
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    char country[kCountryLen];
    uint8_t triplets[];  // TODO(tkilbourn): define these
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.13
struct ExtendedSupportedRatesElement
    : public Element<ExtendedSupportedRatesElement, element_id::kExtSuppRates> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, const std::vector<uint8_t>& rates);
    static const size_t kMinLen = 1;
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    uint8_t rates[];
} __PACKED;

const uint16_t kEapolProtocolId = 0x888E;

// IEEE Std 802.11-2016, 9.4.2.25.1
// The MLME always forwards the RSNE and never requires to decode the element itself.
// Hence, support for accessing optional fields is left out and implemented only by the SME.
struct RsnElement : public Element<RsnElement, element_id::kRsn> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t* raw, size_t raw_len);
    static const size_t kMinLen = 2;
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    uint16_t version;
    uint8_t fields[];
} __PACKED;

}  // namespace wlan
