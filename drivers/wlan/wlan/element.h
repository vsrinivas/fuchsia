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

// IEEE Std 802.11-2016, 9.4.2.56.2
// Note this is a field of HtCapabilities element.
class HtCapabilityInfo : public common::BitField<uint16_t> {
   public:
    constexpr explicit HtCapabilityInfo(uint16_t ht_cap_info)
        : common::BitField<uint16_t>(ht_cap_info) {}
    constexpr HtCapabilityInfo() = default;

    WLAN_BIT_FIELD(ldpc_coding_cap, 0, 1);
    WLAN_BIT_FIELD(chan_width_set, 1, 1);  // In spec: Supported Channel Width Set
    WLAN_BIT_FIELD(sm_power_save, 2, 2);   // Spatial Multiplexing Power Save
    WLAN_BIT_FIELD(greenfield, 4, 1);      // HT-Greenfield.
    WLAN_BIT_FIELD(short_gi_20, 5, 1);     // Short Guard Interval for 20 MHz
    WLAN_BIT_FIELD(short_gi_40, 6, 1);     // Short Guard Interval for 40 MHz
    WLAN_BIT_FIELD(tx_stbc, 7, 1);

    WLAN_BIT_FIELD(rx_stbc, 8, 2);             // maximum number of spatial streams. Up to 3.
    WLAN_BIT_FIELD(delayed_block_ack, 10, 1);  // HT-delayed Block Ack
    WLAN_BIT_FIELD(max_amsdu_len, 11, 1);
    WLAN_BIT_FIELD(dsss_in_40, 12, 1);  // DSSS/CCK Mode in 40 MHz
    WLAN_BIT_FIELD(reserved, 13, 1);
    WLAN_BIT_FIELD(intolerant_40, 14, 1);  // 40 MHz Intolerant
    WLAN_BIT_FIELD(lsig_txop_protect, 15, 1);

    enum ChanWidthSet {
        TWENTY_ONLY = 0,
        TWENTY_FORTY = 1,
    };

    enum SmPowerSave {
        STATIC = 0,
        DYNAMIC = 1,
        RESERVED = 2,
        DISABLED = 3,
    };

    enum MaxAmsduLen {
        OCTETS_3839 = 0,
        OCTETS_7935 = 1,
    };
};

// IEEE Std 802.11-2016, 9.4.2.56.3
class AmpduParams : public common::BitField<uint8_t> {
   public:
    constexpr explicit AmpduParams(uint8_t params) : common::BitField<uint8_t>(params) {}
    constexpr AmpduParams() = default;

    WLAN_BIT_FIELD(exponent, 0, 2);           // Maximum A-MPDU Length Exponent.
    WLAN_BIT_FIELD(min_start_spacing, 2, 3);  // Minimum MPDU Start Spacing.
    WLAN_BIT_FIELD(reserved, 5, 3);

    size_t max_ampdu_len() const { return (1 << (13 + exponent())) - 1; }

    enum MinMPDUStartSpacing {
        NO_RESTRICT = 0,
        QUARTER_USEC = 1,
        HALF_USEC = 2,
        ONE_USEC = 3,
        TWO_USEC = 4,
        FOUR_USEC = 5,
        EIGHT_USEC = 6,
        SIXTEEN_USEC = 7,
    };
};

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsRxMcsHead : public common::BitField<uint64_t> {
   public:
    constexpr explicit SupportedMcsRxMcsHead(uint64_t val) : common::BitField<uint64_t>(val) {}
    constexpr SupportedMcsRxMcsHead() = default;

    // HT-MCS table in IEEE Std 802.11-2016, Annex B.4.17.2
    // VHT-MCS tables in IEEE Std 802.11-2016, 21.5
    WLAN_BIT_FIELD(bitmask, 0, 64);
};

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsRxMcsTail : public common::BitField<uint32_t> {
   public:
    constexpr explicit SupportedMcsRxMcsTail(uint32_t val) : common::BitField<uint32_t>(val) {}
    constexpr SupportedMcsRxMcsTail() = default;

    WLAN_BIT_FIELD(bitmask, 0, 13);
    WLAN_BIT_FIELD(reserved1, 13, 3);
    WLAN_BIT_FIELD(highest_rate, 16, 10);  // Mbps. Rx Highest Supported Rate.
    WLAN_BIT_FIELD(reserved2, 26, 6);
};

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsTxMcs : public common::BitField<uint32_t> {
   public:
    constexpr explicit SupportedMcsTxMcs(uint32_t chunk) : common::BitField<uint32_t>(chunk) {}
    constexpr SupportedMcsTxMcs() = default;

    WLAN_BIT_FIELD(set_defined, 0, 1);  // Add 96 for the original bit location
    WLAN_BIT_FIELD(rx_diff, 1, 1);
    WLAN_BIT_FIELD(max_ss, 2, 2);
    WLAN_BIT_FIELD(ueqm, 4, 1);  // Transmit Unequal Modulation.
    WLAN_BIT_FIELD(reserved, 5, 27);

    uint8_t max_ss_human() const { return max_ss() + 1; }
    void set_max_ss_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_max_ss(num - 1);
    }
};

// IEEE Std 802.11-2016, 9.4.2.56.4
struct SupportedMcsSet {
    SupportedMcsRxMcsHead rx_mcs_head;
    SupportedMcsRxMcsTail rx_mcs_tail;
    SupportedMcsTxMcs tx_mcs;

    // TODO(porce): Implement accessors
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.5
class HtExtCapabilities : public common::BitField<uint16_t> {
   public:
    constexpr explicit HtExtCapabilities(uint16_t ht_ext_cap)
        : common::BitField<uint16_t>(ht_ext_cap) {}
    constexpr HtExtCapabilities() = default;

    WLAN_BIT_FIELD(pco, 0, 1);
    WLAN_BIT_FIELD(pco_transition, 1, 2);
    WLAN_BIT_FIELD(reserved1, 3, 5);
    WLAN_BIT_FIELD(mcs_feedback, 8, 2);
    WLAN_BIT_FIELD(htc_ht_support, 10, 1);
    WLAN_BIT_FIELD(rd_responder, 11, 1);
    WLAN_BIT_FIELD(reserved2, 12, 4);

    enum PcoTransitionTime {
        PCO_RESERVED = 0,
        PCO_400_USEC = 1,
        PCO_1500_USEC = 2,
        PCO_5000_USEC = 3,
    };

    enum McsFeedback {
        MCS_NOFEEDBACK = 0,
        MCS_RESERVED = 1,
        MCS_UNSOLICIED = 2,
        MCS_BOTH = 3,
    };
};

// IEEE Std 802.11-2016, 9.4.2.56.6
class TxBfCapability : public common::BitField<uint32_t> {
   public:
    constexpr explicit TxBfCapability(uint32_t txbf_cap) : common::BitField<uint32_t>(txbf_cap) {}
    constexpr TxBfCapability() = default;

    WLAN_BIT_FIELD(implicit_rx, 0, 1);
    WLAN_BIT_FIELD(rx_stag_sounding, 1, 1);
    WLAN_BIT_FIELD(tx_stag_sounding, 2, 1);
    WLAN_BIT_FIELD(rx_ndp, 3, 1);
    WLAN_BIT_FIELD(tx_ndp, 4, 1);
    WLAN_BIT_FIELD(implicit, 5, 1);
    WLAN_BIT_FIELD(calibration, 6, 2);
    WLAN_BIT_FIELD(csi, 8, 1);  // Explicit CSI Transmit Beamforming.

    WLAN_BIT_FIELD(noncomp_steering, 9, 1);  // Explicit Noncompressed Steering
    WLAN_BIT_FIELD(comp_steering, 10, 1);    // Explicit Compressed Steering
    WLAN_BIT_FIELD(csi_feedback, 11, 2);
    WLAN_BIT_FIELD(noncomp_feedback, 13, 2);
    WLAN_BIT_FIELD(comp_feedback, 15, 2);
    WLAN_BIT_FIELD(min_grouping, 17, 2);
    WLAN_BIT_FIELD(csi_antennas, 19, 2);

    WLAN_BIT_FIELD(noncomp_steering_ants, 21, 2);
    WLAN_BIT_FIELD(comp_steering_ants, 23, 2);
    WLAN_BIT_FIELD(csi_rows, 25, 2);
    WLAN_BIT_FIELD(chan_estimation, 27, 2);
    WLAN_BIT_FIELD(reserved, 29, 3);

    enum Calibration {
        CALIBRATION_NONE = 0,
        CALIBRATION_RESPOND_NOINITIATE = 1,
        CALIBRATION_RESERVED = 2,
        CALIBRATION_RESPOND_INITIATE = 3,
    };

    enum Feedback {
        // Shared for csi_feedback, noncomp_feedback, comp_feedback
        FEEDBACK_NONE = 0,
        FEEDBACK_DELAYED = 1,
        FEEDBACK_IMMEDIATE = 2,
        FEEDBACK_DELAYED_IMMEDIATE = 3,
    };

    enum MinGroup {
        MIN_GROUP_ONE = 0,  // Meaning no grouping
        MIN_GROUP_ONE_TWO = 1,
        MIN_GROUP_ONE_FOUR = 2,
        MIN_GROUP_ONE_TWO_FOUR = 3,
    };

    uint8_t csi_antennas_human() { return csi_antennas() + 1; }
    void set_csi_antennas_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_csi_antennas(num - 1);
    };

    uint8_t noncomp_feedback_human() { return noncomp_feedback() + 1; }
    void set_noncomp_feedback_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_noncomp_feedback(num - 1);
    }

    uint8_t comp_feedback_human() { return comp_feedback() + 1; }
    void set_comp_feedback_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_comp_feedback(num - 1);
    }

    uint8_t chan_estimation_human() { return chan_estimation() + 1; }
    void set_chan_estimation_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_chan_estimation(num - 1);
    }
};

class AselCapability : public common::BitField<uint8_t> {
   public:
    constexpr explicit AselCapability(uint8_t asel_cap) : common::BitField<uint8_t>(asel_cap) {}
    constexpr AselCapability() = default;

    WLAN_BIT_FIELD(asel, 0, 1);
    WLAN_BIT_FIELD(csi_feedback_tx_asel, 1, 1);  // Explicit CSI Feedback based Transmit ASEL
    WLAN_BIT_FIELD(ant_idx_feedback_tx_asel, 2, 1);
    WLAN_BIT_FIELD(explicit_csi_feedback, 3, 1);
    WLAN_BIT_FIELD(antenna_idx_feedback, 4, 1);
    WLAN_BIT_FIELD(rx_asel, 5, 1);
    WLAN_BIT_FIELD(tx_sounding_ppdu, 6, 1);
    WLAN_BIT_FIELD(reserved, 7, 1);
};

// IEEE Std 802.11-2016, 9.4.2.56
struct HtCapabilities : public Element<HtCapabilities, element_id::kHtCapabilities> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, HtCapabilityInfo ht_cap_info,
                       AmpduParams ampdu_params, SupportedMcsSet mcs_set,
                       HtExtCapabilities ht_ext_cap, TxBfCapability txbf_cap,
                       AselCapability asel_cap);
    static constexpr size_t kMinLen = 26;
    static constexpr size_t kMaxLen = 26;

    ElementHeader hdr;
    HtCapabilityInfo ht_cap_info;
    AmpduParams ampdu_params;
    SupportedMcsSet mcs_set;
    HtExtCapabilities ht_ext_cap;
    TxBfCapability txbf_cap;
    AselCapability asel_cap;

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.57
// Note this is a field within HtOperation element.
class HtOpInfoHead : public common::BitField<uint32_t> {
   public:
    constexpr explicit HtOpInfoHead(uint32_t op_info) : common::BitField<uint32_t>(op_info) {}
    constexpr HtOpInfoHead() = default;

    WLAN_BIT_FIELD(secondary_chan_offset, 0, 2);
    WLAN_BIT_FIELD(sta_chan_width, 2, 1);
    WLAN_BIT_FIELD(rifs_mode, 3, 1);
    WLAN_BIT_FIELD(reserved1, 4, 4);

    WLAN_BIT_FIELD(ht_protect, 8, 2);
    WLAN_BIT_FIELD(nongreenfield_present, 10, 1);  // Nongreenfield HT STAs present.
    WLAN_BIT_FIELD(reserved2, 11, 1);
    WLAN_BIT_FIELD(obss_non_ht, 12, 1);  // OBSS Non-HT STAs present.
    WLAN_BIT_FIELD(center_freq_seg2, 13, 11);
    WLAN_BIT_FIELD(reserved3, 21, 2);

    WLAN_BIT_FIELD(reserved4, 24, 6);
    WLAN_BIT_FIELD(dual_beacon, 30, 1);
    WLAN_BIT_FIELD(dual_cts_protect, 31, 1);

    enum SecChanOffset {
        SECONDARY_NONE = 0,   // No secondary channel
        SECONDARY_ABOVE = 1,  // Secondary channel is above the primary channel
        RESERVED = 2,
        SECONDARY_BELOW = 3,  // Secondary channel is below the primary channel
    };

    enum StaChanWidth {
        TWENTY = 0,  // MHz
        ANY = 1,     // Any in the Supported Channel Width set
    };

    enum HtProtect {
        NONE = 0,
        NONMEMBER = 1,
        TWENTY_MHZ = 2,
        NON_HT_MIXED = 3,
    };
};

class HtOpInfoTail : public common::BitField<uint8_t> {
    constexpr explicit HtOpInfoTail(uint8_t) : common::BitField<uint8_t>() {}
    constexpr HtOpInfoTail() = default;

    WLAN_BIT_FIELD(stbc_beacon, 0, 1);  // Add 32 for the original bit location.
    WLAN_BIT_FIELD(lsig_txop_protect, 1, 1);
    WLAN_BIT_FIELD(pco_active, 2, 1);
    WLAN_BIT_FIELD(pco_phase, 3, 1);
    WLAN_BIT_FIELD(reserved5, 4, 4);
};

// IEEE Std 802.11-2016, 9.4.2.57
struct HtOperation : public Element<TimElement, element_id::kHtOperation> {
    bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t primary_chan, HtOpInfoHead head,
                HtOpInfoTail tail, SupportedMcsSet mcs_set);
    static constexpr size_t kMinLen = 22;
    static constexpr size_t kMaxLen = 22;

    ElementHeader hdr;

    uint8_t primary_chan;  // Primary 20 MHz channel.

    // Implementation hack to support 40bits bitmap.
    HtOpInfoHead head;
    HtOpInfoTail tail;
    SupportedMcsSet mcs_set;
} __PACKED;

}  // namespace wlan
